#include <Arduino_AdvancedAnalog.h>
#include "duinoscope.h"
#include "lvgl.h"
#include "Arduino_H7_Video.h"
#include "Arduino_GigaDisplayTouch.h"
#include <Arduino_GigaDisplay.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "duinoscope_html.h"

const char* FIRMWARE_VERSION = "1.0.0";

String ch0_telemetry_str = "Standby";
String ch1_telemetry_str = "Standby";
uint32_t last_http_request_time = 0;
IPAddress apIP;

WiFiServer httpServer(80);
WiFiUDP dnsServer;
const byte DNS_PORT = 53;

int getQueryVal(String path, String param) {
  int pIdx = path.indexOf(param + "=");
  if (pIdx == -1) return -1;
  int start = pIdx + param.length() + 1;
  int end = path.indexOf('&', start);
  if (end == -1) {
    return path.substring(start).toInt();
  }
  return path.substring(start, end).toInt();
}

AdvancedADC adc(A0, A1);
uint64_t last_millis = 0;

Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch TouchDetector;
GigaDisplayRGB rgbLed;

static uint8_t led_brightness = 64;

void setLEDs(bool red, bool green, bool blue, uint8_t brightness = led_brightness)
{

  if (red || green || blue)
  {
    rgbLed.on(red ? brightness : 0, green ? brightness : 0, blue ? brightness : 0);
  }
  else
  {
    rgbLed.off();
  }
}

#define VAL_OFF 0
#define VAL_DIM 1
#define VAL_ON  2

void updateBoardLEDs()
{
  uint32_t now = millis();

  bool wifi_connected = (millis() - last_http_request_time < 5000);

  bool wifi_blink_on = ((now / 250) % 2 == 0);

  bool ch0_active = false;
  bool ch1_active = false;

  for (int ch = 0; ch < 2; ch++)
  {
    if (chan[ch].shown)
    {
      float freq = chan[ch].freq;
      float vpp = chan[ch].v_max - chan[ch].v_min;
      int y_ind = chan[ch].y_index;
      int r = voltage[y_ind].range_idx;
      float v_range = V_RANGE(r);
      float v_min_limit = V_MIN(r);

      if ((freq >= 1.0f) ||
          (vpp > 0.05f * v_range) ||
          (fabsf(chan[ch].v_min - v_min_limit) > 0.05f * v_range))
      {
        if (ch == 0) ch0_active = true;
        if (ch == 1) ch1_active = true;
      }
    }
  }

  bool activity_blink_on = ((now / 50) % 2 == 0);

  bool show_ch0 = ch0_active && activity_blink_on;
  bool show_ch1 = ch1_active && activity_blink_on;

  if (ch0_active && ch1_active)
  {
    bool alternate_phase = ((now / 100) % 2 == 0);
    show_ch0 = alternate_phase && activity_blink_on;
    show_ch1 = !alternate_phase && activity_blink_on;
  }

  int red_target = VAL_OFF;
  int green_target = VAL_OFF;
  int blue_target = VAL_OFF;

  if (ch0_active || ch1_active)
  {
    if (show_ch0)
    {

      red_target = VAL_ON;
      green_target = VAL_ON;
    }
    else if (show_ch1)
    {

      green_target = VAL_ON;
      blue_target = VAL_ON;
    }
  }
  else
  {

    green_target = VAL_DIM;
  }

  int blue_duty = 0;

  uint32_t pulse_phase = now % 2000;
  int pulse_brightness = 0;
  if (pulse_phase < 1000)
  {
    pulse_brightness = (pulse_phase * 8) / 1000;
  }
  else
  {
    pulse_brightness = ((2000 - pulse_phase) * 8) / 1000;
  }

  if (blue_target == VAL_ON)
  {
    blue_duty = 10;
  }
  else if (!wifi_connected)
  {
    blue_duty = pulse_brightness;
  }
  else
  {
    blue_duty = 1;
  }

  uint32_t pwm_cycle = now % 10;

  if (red_target == VAL_ON)
  {
    digitalWrite(LEDR, LOW);
  }
  else if (red_target == VAL_DIM && pwm_cycle == 0)
  {
    digitalWrite(LEDR, LOW);
  }
  else
  {
    digitalWrite(LEDR, HIGH);
  }

  if (green_target == VAL_ON)
  {
    digitalWrite(LEDG, LOW);
  }
  else if (green_target == VAL_DIM && pwm_cycle == 0)
  {
    digitalWrite(LEDG, LOW);
  }
  else
  {
    digitalWrite(LEDG, HIGH);
  }

  if (pwm_cycle < blue_duty)
  {
    digitalWrite(LEDB, LOW);
  }
  else
  {
    digitalWrite(LEDB, HIGH);
  }
}

int16_t sample_data[2 * N_SAMPLES];
int sample_count = 0;

static bool dragging = false;
static bool pinching = false;
static lv_point_t start_point;
static float orig_trig_level;
static int orig_trig_x;
static int orig_yoffset[2];
static float orig_vdiv[2];
static float orig_tdiv;
static float orig_pinch_dist = 0;

static float last_printed_freq[2] = {0.0f, 0.0f};
static float last_printed_vpp[2] = {0.0f, 0.0f};
static bool last_printed_shown[2] = {false, false};
static uint32_t last_telemetry_time[2] = {0, 0};

lv_obj_t * btn_ch0;
lv_obj_t * btn_ch1;
lv_obj_t * dd_ch0_vdiv;
lv_obj_t * dd_ch1_vdiv;
lv_obj_t * dd_tb;
lv_obj_t * dd_trig;
lv_obj_t * scope_viewport;

lv_obj_t * lbl_ch0_info;
lv_obj_t * lbl_ch1_info;
lv_obj_t * lbl_trig_info;
lv_obj_t * lbl_tb_info;

void tb_tdiv_str(int indx, char *str)
{
  if (tb[indx].t_div >= 1000)
    sprintf(str, "%.0f ms", tb[indx].t_div / 1000);
  else
    sprintf(str, "%.0f us", tb[indx].t_div);
}

void tb_sps_str(int indx, char *str)
{
  if (tb[indx].sps >= 1000000)
    sprintf(str, "(%d Msps)", tb[indx].sps / 1000000);
  else
    sprintf(str, "(%d ksps)", tb[indx].sps / 1000);
}

void ch_vdiv_str(int ch, char *str)
{
  if (voltage[ch].v_div < 1.0f)
    sprintf(str, "%.1f V", voltage[ch].v_div);
  else
    sprintf(str, "%.0f V", voltage[ch].v_div);
}

void ch_freq_str(int ch, char *str)
{
  float freq = chan[ch].freq;

  if (freq < 1)
    str[0] = '\0';
  else if (freq < 1000.0f)
    sprintf(str, "%.0f Hz", freq);
  else if (freq < 10000.0f)
    sprintf(str, "%.2f kHz", freq / 1000);
  else
    sprintf(str, "%.0f kHz", freq / 1000);
}

void ch_volt_str(int ch, char *str)
{
  float v = chan[ch].v_max - chan[ch].v_min;
  if (v > 0.001)
  {
    sprintf(str, " %.2f Vp-p", v);
  }
  else
  {
    sprintf(str, " %.2f V", chan[ch].v_min);
  }
}

int find_next_trigger(const int16_t *buf, int size, int ch, int start_pos)
{
  int y_ind = chan[ch].y_index;
  int r = voltage[y_ind].range_idx;
  int adc_count = ADC_RANGE * (trig_level - V_MIN(r)) / V_RANGE(r);
  int hyst = ADC_RANGE * range[r].level_hyst / V_RANGE(r);
  int i;

  switch (trig)
  {
  case 0:
  case 1:
    i = start_pos;
    while (i < size && buf[i] >= adc_count - hyst)
      i += 2;
    if (i >= size)
      return 0;

    while (i < size && buf[i] < adc_count)
      i += 2;
    if (i >= size)
      return 0;
    break;

  case 2:
    i = start_pos;
    while (i < size && buf[i] <= adc_count + hyst)
      i += 2;
    if (i >= size)
      return 0;

    while (i < size && buf[i] > adc_count)
      i += 2;
    if (i >= size)
      return 0;
    break;
  }
  return i;
}

void find_trig_and_freq(const int16_t *buf, int size, int start_pos)
{
  int trig_pos;
  int prev_trig = find_next_trigger(buf, size, start_pos, start_pos);
  int n_trig = 0;
  long spacing = 0;

  chan[start_pos].trig_pt = 0;
  chan[start_pos].freq = 0;
  if (prev_trig == 0)
    return;
  chan[start_pos].trig_pt = prev_trig;

  while ((trig_pos = find_next_trigger(buf, size, start_pos, prev_trig)) != 0)
  {
    spacing += (trig_pos - prev_trig) / 2;
    n_trig++;
    prev_trig = trig_pos;
  }
  if (n_trig == 0)
    return;
  chan[start_pos].freq = tb[x_index].sps / (spacing / n_trig);
}

void process_buffer(const int16_t *buf, int size)
{
  for (int ch = 0; ch < 2; ch++)
  {
    int y_off = chan[ch].y_offset;
    int y_ind = chan[ch].y_index;
    int r = voltage[y_ind].range_idx;

    chan[ch].y_min = 9999;
    chan[ch].y_max = 0;
    chan[ch].v_min = 99999.0f;
    chan[ch].v_max = -99999.0f;

    for (int i = 0, p = ch; p < size; i++, p += 2)
    {
      int y = y_off - (buf[p] - SIGN_OFFSET(r)) * voltage[y_ind].pix_count;
      float v = (buf[p] - SIGN_OFFSET(r)) * V_RANGE(r) / ADC_RANGE;

      if (y < chan[ch].y_min)
      {
        chan[ch].y_min = y;
        chan[ch].v_max = v;
      }
      if (y > chan[ch].y_max)
      {
        chan[ch].y_max = y;
        chan[ch].v_min = v;
      }
    }
  }
}

void update_labels()
{
  char str[64];

  if (chan[0].shown)
  {
    char f_str[16];
    char v_str[16];
    ch_freq_str(0, f_str);
    ch_volt_str(0, v_str);
    if (f_str[0] == '\0')
      sprintf(str, "CH0: %s", v_str);
    else
      sprintf(str, "CH0: %s %s", f_str, v_str);
    lv_label_set_text(lbl_ch0_info, str);
  }
  else
  {
    lv_label_set_text(lbl_ch0_info, "CH0: Off");
  }

  if (chan[1].shown)
  {
    char f_str[16];
    char v_str[16];
    ch_freq_str(1, f_str);
    ch_volt_str(1, v_str);
    if (f_str[0] == '\0')
      sprintf(str, "CH1: %s", v_str);
    else
      sprintf(str, "CH1: %s %s", f_str, v_str);
    lv_label_set_text(lbl_ch1_info, str);
  }
  else
  {
    lv_label_set_text(lbl_ch1_info, "CH1: Off");
  }

  if (trig != 0)
  {
    sprintf(str, "Trig: %.2fV (CH%d/%s)", trig_level, trig_ch, (trig == 1) ? "R" : "F");
    lv_label_set_text(lbl_trig_info, str);
  }
  else
  {
    lv_label_set_text(lbl_trig_info, "Trig: Off");
  }

  char tb_div_str[16];
  char sps_str[16];
  tb_tdiv_str(x_index, tb_div_str);
  tb_sps_str(x_index, sps_str);
  sprintf(str, "%s %s", tb_div_str, sps_str);
  lv_label_set_text(lbl_tb_info, str);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  GDTpoint_t points[5];
  uint8_t contacts = TouchDetector.getTouchPoints(points);

  if (contacts > 0)
  {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = points[0].x;
    data->point.y = points[0].y;
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void btn_ch_toggle_cb(lv_event_t *e)
{
  lv_obj_t *btn = lv_event_get_target(e);
  int ch = (int)lv_event_get_user_data(e);
  bool checked = lv_obj_has_state(btn, LV_STATE_CHECKED);
  chan[ch].shown = checked;

  if (checked)
  {
    lv_obj_set_style_bg_color(btn, lv_color_hex(chan[ch].color), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_PART_MAIN);
  }
  else
  {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x888888), LV_PART_MAIN);
  }
  lv_obj_invalidate(scope_viewport);

  Serial.print("CH"); Serial.print(ch); Serial.print(" status set to: ");
  Serial.println(checked ? "ENABLED" : "DISABLED");
}

static void dd_vdiv_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target(e);
  int ch = (int)lv_event_get_user_data(e);
  int indx = lv_dropdown_get_selected(dd);

  chan[ch].y_index = indx;

  int r = voltage[indx].range_idx;
  digitalWrite(chan[ch].pin1, (r >> 1));
  digitalWrite(chan[ch].pin0, (r & 1));
  lv_obj_invalidate(scope_viewport);

  char v_str[16];
  ch_vdiv_str(indx, v_str);
  Serial.print("CH"); Serial.print(ch); Serial.print(" Volts/Div set to: ");
  Serial.print(v_str); Serial.print(" (AFE range: "); Serial.print(r); Serial.println(")");
}

static void dd_tb_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target(e);
  int indx = lv_dropdown_get_selected(dd);

  x_index = indx;

  adc.stop();
  adc.begin(ADC_RESOLUTION, tb[x_index].sps, N_SAMPLES, 32);
  lv_obj_invalidate(scope_viewport);

  char t_str[16];
  tb_tdiv_str(x_index, t_str);
  char sps_str[16];
  tb_sps_str(x_index, sps_str);
  Serial.print("Timebase set to: "); Serial.print(t_str);
  Serial.print(" "); Serial.println(sps_str);
}

static void dd_trig_cb(lv_event_t *e)
{
  lv_obj_t *dd = lv_event_get_target(e);
  int indx = lv_dropdown_get_selected(dd);

  int new_trig_ch = 0;
  if (indx == 3 || indx == 4)
  {
    new_trig_ch = 1;
  }

  int y_ind = chan[new_trig_ch].y_index;
  int r = voltage[y_ind].range_idx;

  switch (indx)
  {
  case 0:
    trig = 0;
    trig_level = range[r].rising_level;
    break;
  case 1:
    trig = 1;
    trig_ch = 0;
    trig_level = range[r].rising_level;
    break;
  case 2:
    trig = 2;
    trig_ch = 0;
    trig_level = range[r].falling_level;
    break;
  case 3:
    trig = 1;
    trig_ch = 1;
    trig_level = range[r].rising_level;
    break;
  case 4:
    trig = 2;
    trig_ch = 1;
    trig_level = range[r].falling_level;
    break;
  }
  lv_obj_invalidate(scope_viewport);

  Serial.print("Trigger set to: ");
  if (trig == 0)
  {
    Serial.println("OFF");
  }
  else
  {
    Serial.print((trig == 1) ? "Rising" : "Falling");
    Serial.print(" CH"); Serial.print(trig_ch);
    Serial.print(", Level: "); Serial.print(trig_level); Serial.println("V");
  }
}

static void viewport_draw_cb(lv_event_t *e)
{
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);

  lv_draw_line_dsc_t grid_dsc;
  lv_draw_line_dsc_init(&grid_dsc);
  grid_dsc.color = lv_color_hex(0x2E2E2E);
  grid_dsc.width = 1;

  for (int y = 80; y <= 400; y += PIX_DIV)
  {
    if (y >= coords.y1 && y <= coords.y2)
    {
      lv_point_t p1 = {coords.x1, y};
      lv_point_t p2 = {coords.x2, y};
      lv_draw_line(draw_ctx, &grid_dsc, &p1, &p2);
    }
  }

  for (int x = 80; x <= 720; x += PIX_DIV)
  {
    if (x >= coords.x1 && x <= coords.x2)
    {
      lv_point_t p1 = {x, coords.y1};
      lv_point_t p2 = {x, coords.y2};
      lv_draw_line(draw_ctx, &grid_dsc, &p1, &p2);
    }
  }

  for (int ch = 0; ch < 2; ch++)
  {
    if (chan[ch].shown)
    {
      int y_off = chan[ch].y_offset;
      int y_ind = chan[ch].y_index;
      int r = voltage[y_ind].range_idx;

      lv_draw_line_dsc_t trace_dsc;
      lv_draw_line_dsc_init(&trace_dsc);
      trace_dsc.color = lv_color_hex(chan[ch].color);
      trace_dsc.width = 2;

      int p_sam = tb[x_index].p_sam;
      int width = coords.x2 - coords.x1;

      int i_start = (-x_offset - trig_x - p_sam) / p_sam;
      if (i_start < 0)
        i_start = 0;

      int i_end = (width - x_offset - trig_x + p_sam) / p_sam;
      int max_i = (sample_count - ch - 2) / 2;
      if (i_end > max_i)
        i_end = max_i;

      if (i_start <= i_end)
      {
        int prev_x = coords.x1 + x_offset + trig_x + i_start * p_sam;
        int p_start = ch + 2 * i_start;
        int prev_y = y_off - (sample_data[p_start] - SIGN_OFFSET(r)) * voltage[y_ind].pix_count;

        for (int i = i_start + 1; i <= i_end + 1; i++)
        {
          int p = ch + 2 * i;
          if (p >= sample_count)
            break;
          int x = coords.x1 + x_offset + trig_x + i * p_sam;
          int y = y_off - (sample_data[p] - SIGN_OFFSET(r)) * voltage[y_ind].pix_count;

          lv_point_t p1 = {prev_x, prev_y};
          lv_point_t p2 = {x, y};
          lv_draw_line(draw_ctx, &trace_dsc, &p1, &p2);

          prev_x = x;
          prev_y = y;
        }
      }

      if (y_off >= coords.y1 && y_off <= coords.y2)
      {
        lv_draw_line_dsc_t ind_dsc;
        lv_draw_line_dsc_init(&ind_dsc);
        ind_dsc.color = lv_color_hex(chan[ch].color);
        ind_dsc.width = 2;

        lv_point_t tp1 = {coords.x1 + 2, y_off - 5};
        lv_point_t tp2 = {coords.x1 + 8, y_off};
        lv_point_t tp3 = {coords.x1 + 2, y_off + 5};
        lv_draw_line(draw_ctx, &ind_dsc, &tp1, &tp2);
        lv_draw_line(draw_ctx, &ind_dsc, &tp2, &tp3);
        lv_draw_line(draw_ctx, &ind_dsc, &tp3, &tp1);
      }
    }
  }

  if (trig != 0)
  {
    int y_ind = chan[trig_ch].y_index;
    int r = voltage[y_ind].range_idx;
    int adc_count = ADC_RANGE * (trig_level - V_MIN(r)) / V_RANGE(r);
    int local_y_trig = chan[trig_ch].y_offset - (adc_count - SIGN_OFFSET(r)) * voltage[y_ind].pix_count;

    lv_draw_line_dsc_t trig_dsc;
    lv_draw_line_dsc_init(&trig_dsc);
    trig_dsc.color = lv_color_hex(chan[trig_ch].color);
    trig_dsc.width = 2;

    if (local_y_trig >= coords.y1 + 5 && local_y_trig <= coords.y2 - 5)
    {
      lv_point_t hp1 = {coords.x1 + 2, local_y_trig - 4};
      lv_point_t hp2 = {coords.x1 + 12, local_y_trig - 4};
      lv_draw_line(draw_ctx, &trig_dsc, &hp1, &hp2);
      lv_point_t vp1 = {coords.x1 + 7, local_y_trig - 4};
      lv_point_t vp2 = {coords.x1 + 7, local_y_trig + 6};
      lv_draw_line(draw_ctx, &trig_dsc, &vp1, &vp2);
    }

    int tx = coords.x1 + trig_x;
    if (tx >= coords.x1 + 5 && tx <= coords.x2 - 5)
    {
      int ty = coords.y1 + 2;
      lv_point_t xp1 = {tx - 5, ty};
      lv_point_t xp2 = {tx + 5, ty};
      lv_point_t xp3 = {tx, ty + 6};
      lv_draw_line(draw_ctx, &trig_dsc, &xp1, &xp2);
      lv_draw_line(draw_ctx, &trig_dsc, &xp2, &xp3);
      lv_draw_line(draw_ctx, &trig_dsc, &xp3, &xp1);
    }
  }
}

static void viewport_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED)
  {
    lv_indev_t *indev = lv_indev_get_act();
    lv_indev_get_point(indev, &start_point);
    dragging = true;
    pinching = false;

    orig_trig_level = trig_level;
    orig_trig_x = trig_x;
    orig_yoffset[0] = chan[0].y_offset;
    orig_yoffset[1] = chan[1].y_offset;
    orig_vdiv[0] = voltage[chan[0].y_index].v_div;
    orig_vdiv[1] = voltage[chan[1].y_index].v_div;
    orig_tdiv = tb[x_index].t_div;
  }
  else if (code == LV_EVENT_PRESSING)
  {
    GDTpoint_t touch_points[5];
    uint8_t contacts = TouchDetector.getTouchPoints(touch_points);

    if (contacts == 2)
    {
      dragging = false;
      float dx = touch_points[0].x - touch_points[1].x;
      float dy = touch_points[0].y - touch_points[1].y;
      float dist = sqrt(dx * dx + dy * dy);

      if (!pinching)
      {
        pinching = true;
        orig_pinch_dist = dist;
      }
      else if (orig_pinch_dist > 0)
      {
        float ratio = dist / orig_pinch_dist;
        if (ratio > 0.1)
        {
          if (abs(touch_points[0].x - touch_points[1].x) > abs(touch_points[0].y - touch_points[1].y))
          {

            float target_tdiv = orig_tdiv / ratio;
            float min_diff = 1e6;
            int min_idx = x_index;
            for (int i = 0; i < TB_MAX; i++)
            {
              float diff = abs(target_tdiv - tb[i].t_div);
              if (diff < min_diff)
              {
                min_diff = diff;
                min_idx = i;
              }
            }
            if (min_idx != x_index)
            {
              x_index = min_idx;
              adc.stop();
              adc.begin(ADC_RESOLUTION, tb[x_index].sps, N_SAMPLES, 32);
              lv_dropdown_set_selected(dd_tb, x_index);
              lv_obj_invalidate(scope_viewport);
            }
          }
          else
          {

            int ch = chan[0].shown ? 0 : (chan[1].shown ? 1 : -1);
            if (ch != -1)
            {
              float target_vdiv = orig_vdiv[ch] / ratio;
              float min_diff = 1e6;
              int min_idx = chan[ch].y_index;
              for (int i = 0; i < VOLTS_MAX; i++)
              {
                float diff = abs(target_vdiv - voltage[i].v_div);
                if (diff < min_diff)
                {
                  min_diff = diff;
                  min_idx = i;
                }
              }
              if (min_idx != chan[ch].y_index)
              {
                chan[ch].y_index = min_idx;
                int r = voltage[min_idx].range_idx;
                digitalWrite(chan[ch].pin1, (r >> 1));
                digitalWrite(chan[ch].pin0, (r & 1));
                if (ch == 0)
                {
                  lv_dropdown_set_selected(dd_ch0_vdiv, min_idx);
                }
                else
                {
                  lv_dropdown_set_selected(dd_ch1_vdiv, min_idx);
                }
                lv_obj_invalidate(scope_viewport);
              }
            }
          }
        }
      }
    }
    else if (contacts == 1 && dragging)
    {
      lv_indev_t *indev = lv_indev_get_act();
      lv_point_t curr_point;
      lv_indev_get_point(indev, &curr_point);

      int dx = curr_point.x - start_point.x;
      int dy = curr_point.y - start_point.y;

      if (start_point.x < 100 && trig != 0)
      {

        int y_ind = chan[trig_ch].y_index;
        int r = voltage[y_ind].range_idx;
        trig_level = orig_trig_level - (dy / voltage[y_ind].pix_count) * V_RANGE(r) / ADC_RANGE;
        if (trig_level > V_MAX(r))
          trig_level = V_MAX(r);
        if (trig_level < V_MIN(r))
          trig_level = V_MIN(r);
      }
      else
      {

        trig_x = orig_trig_x + dx;

        int ch = chan[0].shown ? 0 : (chan[1].shown ? 1 : -1);
        if (ch != -1)
        {
          chan[ch].y_offset = orig_yoffset[ch] + dy;
        }
      }
      lv_obj_invalidate(scope_viewport);
    }
  }
  else if (code == LV_EVENT_RELEASED)
  {
    if (dragging)
    {
      Serial.print("Touch gesture drag finished. Offset CH0: ");
      Serial.print(chan[0].y_offset);
      Serial.print(", CH1: ");
      Serial.print(chan[1].y_offset);
      Serial.print(", Trig X: ");
      Serial.print(trig_x);
      if (trig != 0)
      {
        Serial.print(", Trig Level: ");
        Serial.print(trig_level);
        Serial.print("V");
      }
      Serial.println();
    }
    if (pinching)
    {
      char t_str[16];
      tb_tdiv_str(x_index, t_str);
      char v0_str[16];
      char v1_str[16];
      ch_vdiv_str(chan[0].y_index, v0_str);
      ch_vdiv_str(chan[1].y_index, v1_str);
      Serial.print("Touch gesture pinch zoom finished. Timebase: ");
      Serial.print(t_str);
      Serial.print(", Volts/Div CH0: ");
      Serial.print(v0_str);
      Serial.print(", CH1: ");
      Serial.print(v1_str);
      Serial.println();
    }
    dragging = false;
    pinching = false;
  }
}

void setup()
{
  Serial.begin(9600);
  while (!Serial && millis() < 3000);
  Serial.println("\n--- DuinoScope GIGA R1 Debug Monitor ---");

  if (!adc.begin(ADC_RESOLUTION, tb[x_index].sps, N_SAMPLES, 32))
  {
    Serial.println("Failed to start analog acquisition!");
    while (1)
      ;
  }
  Serial.println("ADC acquisition started successfully.");

  Display.begin();
  Serial.println("Display initialization completed.");
  TouchDetector.begin();
  Serial.println("Touch controller initialization completed.");

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  digitalWrite(3, LOW);
  digitalWrite(4, LOW);
  digitalWrite(5, LOW);
  digitalWrite(6, LOW);

  pinMode(2, OUTPUT);
  tone(2, 1000);

  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDR, HIGH);
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDB, HIGH);

  rgbLed.begin();

  lv_obj_t *header_bar = lv_obj_create(lv_scr_act());
  lv_obj_set_size(header_bar, 800, 55);
  lv_obj_set_pos(header_bar, 0, 0);
  lv_obj_set_style_bg_color(header_bar, lv_color_hex(0x1F1F1F), LV_PART_MAIN);
  lv_obj_set_style_border_width(header_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(header_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(header_bar, 0, LV_PART_MAIN);

  btn_ch0 = lv_btn_create(header_bar);
  lv_obj_set_size(btn_ch0, 85, 45);
  lv_obj_set_pos(btn_ch0, 10, 5);
  lv_obj_add_flag(btn_ch0, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_t *lbl_ch0 = lv_label_create(btn_ch0);
  lv_label_set_text(lbl_ch0, "CH0");
  lv_obj_center(lbl_ch0);
  lv_obj_add_event_cb(btn_ch0, btn_ch_toggle_cb, LV_EVENT_VALUE_CHANGED, (void *)0);
  if (chan[0].shown)
  {
    lv_obj_add_state(btn_ch0, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn_ch0, lv_color_hex(chan[0].color), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_ch0, lv_color_black(), LV_PART_MAIN);
  }
  else
  {
    lv_obj_set_style_bg_color(btn_ch0, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_ch0, lv_color_hex(0x888888), LV_PART_MAIN);
  }

  dd_ch0_vdiv = lv_dropdown_create(header_bar);
  lv_obj_set_size(dd_ch0_vdiv, 115, 45);
  lv_obj_set_pos(dd_ch0_vdiv, 105, 5);
  lv_dropdown_set_options(dd_ch0_vdiv, "0.1 V\n0.2 V\n0.5 V\n1.0 V\n2.0 V");
  lv_dropdown_set_selected(dd_ch0_vdiv, chan[0].y_index);
  lv_obj_add_event_cb(dd_ch0_vdiv, dd_vdiv_cb, LV_EVENT_VALUE_CHANGED, (void *)0);

  btn_ch1 = lv_btn_create(header_bar);
  lv_obj_set_size(btn_ch1, 85, 45);
  lv_obj_set_pos(btn_ch1, 235, 5);
  lv_obj_add_flag(btn_ch1, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_t *lbl_ch1 = lv_label_create(btn_ch1);
  lv_label_set_text(lbl_ch1, "CH1");
  lv_obj_center(lbl_ch1);
  lv_obj_add_event_cb(btn_ch1, btn_ch_toggle_cb, LV_EVENT_VALUE_CHANGED, (void *)1);
  if (chan[1].shown)
  {
    lv_obj_add_state(btn_ch1, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn_ch1, lv_color_hex(chan[1].color), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_ch1, lv_color_black(), LV_PART_MAIN);
  }
  else
  {
    lv_obj_set_style_bg_color(btn_ch1, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_ch1, lv_color_hex(0x888888), LV_PART_MAIN);
  }

  dd_ch1_vdiv = lv_dropdown_create(header_bar);
  lv_obj_set_size(dd_ch1_vdiv, 115, 45);
  lv_obj_set_pos(dd_ch1_vdiv, 330, 5);
  lv_dropdown_set_options(dd_ch1_vdiv, "0.1 V\n0.2 V\n0.5 V\n1.0 V\n2.0 V");
  lv_dropdown_set_selected(dd_ch1_vdiv, chan[1].y_index);
  lv_obj_add_event_cb(dd_ch1_vdiv, dd_vdiv_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

  dd_tb = lv_dropdown_create(header_bar);
  lv_obj_set_size(dd_tb, 125, 45);
  lv_obj_set_pos(dd_tb, 460, 5);
  lv_dropdown_set_options(dd_tb, "10 us\n20 us\n50 us\n100 us\n200 us\n500 us\n1 ms\n2 ms\n5 ms\n10 ms");
  lv_dropdown_set_selected(dd_tb, x_index);
  lv_obj_add_event_cb(dd_tb, dd_tb_cb, LV_EVENT_VALUE_CHANGED, NULL);

  dd_trig = lv_dropdown_create(header_bar);
  lv_obj_set_size(dd_trig, 190, 45);
  lv_obj_set_pos(dd_trig, 600, 5);
  lv_dropdown_set_options(dd_trig, "Trig: Off\nTrig: Rising CH0\nTrig: Falling CH0\nTrig: Rising CH1\nTrig: Falling CH1");
  int trig_sel = 0;
  if (trig != 0)
    trig_sel = (trig_ch == 0) ? trig : trig + 2;
  lv_dropdown_set_selected(dd_trig, trig_sel);
  lv_obj_add_event_cb(dd_trig, dd_trig_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
  lv_obj_set_size(status_bar, 800, 40);
  lv_obj_set_pos(status_bar, 0, 440);
  lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x151515), LV_PART_MAIN);
  lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(status_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(status_bar, 0, LV_PART_MAIN);

  lbl_ch0_info = lv_label_create(status_bar);
  lv_obj_set_pos(lbl_ch0_info, 15, 10);
  lv_obj_set_style_text_color(lbl_ch0_info, lv_color_hex(chan[0].color), LV_PART_MAIN);
  lv_label_set_text(lbl_ch0_info, "CH0: --");

  lbl_ch1_info = lv_label_create(status_bar);
  lv_obj_set_pos(lbl_ch1_info, 260, 10);
  lv_obj_set_style_text_color(lbl_ch1_info, lv_color_hex(chan[1].color), LV_PART_MAIN);
  lv_label_set_text(lbl_ch1_info, "CH1: --");

  lbl_trig_info = lv_label_create(status_bar);
  lv_obj_set_pos(lbl_trig_info, 500, 10);
  lv_obj_set_style_text_color(lbl_trig_info, lv_color_white(), LV_PART_MAIN);
  lv_label_set_text(lbl_trig_info, "Trig: Off");

  lbl_tb_info = lv_label_create(status_bar);
  lv_obj_set_pos(lbl_tb_info, 680, 10);
  lv_obj_set_style_text_color(lbl_tb_info, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
  lv_label_set_text(lbl_tb_info, "--");

  scope_viewport = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scope_viewport, 800, 385);
  lv_obj_set_pos(scope_viewport, 0, 55);
  lv_obj_set_style_bg_color(scope_viewport, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_width(scope_viewport, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(scope_viewport, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(scope_viewport, 0, LV_PART_MAIN);

  lv_obj_add_event_cb(scope_viewport, viewport_draw_cb, LV_EVENT_DRAW_POST, NULL);
  lv_obj_add_event_cb(scope_viewport, viewport_event_cb, LV_EVENT_ALL, NULL);

  for (int ch = 0; ch < 2; ch++)
  {
    int r = voltage[chan[ch].y_index].range_idx;
    digitalWrite(chan[ch].pin1, (r >> 1));
    digitalWrite(chan[ch].pin0, (r & 1));
  }

  int wifi_status = WiFi.beginAP("DuinoScope", "12345678");
  if (wifi_status != WL_AP_LISTENING)
  {
    Serial.println("Creating Access Point failed!");
  }
  else
  {
    Serial.println("Access Point 'DuinoScope GIGA R1' created successfully.");
    apIP = WiFi.localIP();
    Serial.print("AP IP Address: ");
    Serial.println(apIP);
    dnsServer.begin(DNS_PORT);
    httpServer.begin();
    Serial.println("DNS and HTTP Servers started successfully.");
  }
}

void loop()
{

  lv_timer_handler();

  {
    int packetSize = dnsServer.parsePacket();
    if (packetSize > 0)
    {
      byte packet[512];
      int len = dnsServer.read(packet, 512);
      if (len >= 12 && ((packet[2] & 0x80) == 0))
      {
        packet[2] = 0x84;
        packet[3] = 0x00;
        packet[6] = 0x00; packet[7] = 0x01;

        int idx = 12;
        while (idx < len && packet[idx] != 0)
        {
          idx += packet[idx] + 1;
        }
        idx++;
        idx += 4;

        if (idx + 16 <= 512)
        {
          packet[idx++] = 0xc0; packet[idx++] = 0x0c;
          packet[idx++] = 0x00; packet[idx++] = 0x01;
          packet[idx++] = 0x00; packet[idx++] = 0x01;
          packet[idx++] = 0x00; packet[idx++] = 0x00; packet[idx++] = 0x00; packet[idx++] = 0x3c;
          packet[idx++] = 0x00; packet[idx++] = 0x04;
          packet[idx++] = apIP[0];  packet[idx++] = apIP[1];  packet[idx++] = apIP[2];   packet[idx++] = apIP[3];

          dnsServer.beginPacket(dnsServer.remoteIP(), dnsServer.remotePort());
          dnsServer.write(packet, idx);
          dnsServer.endPacket();
        }
      }
    }
  }

  {
    WiFiClient client = httpServer.available();
    if (client)
    {
      last_http_request_time = millis();
      String req = "";
      while (client.connected())
      {
        if (client.available())
        {
          char c = client.read();
          req += c;
          if (req.endsWith("\r\n\r\n"))
          {
            break;
          }
        }
      }

      if (req.length() > 0)
      {
        int space1 = req.indexOf(' ');
        int space2 = req.indexOf(' ', space1 + 1);
        if (space1 != -1 && space2 != -1)
        {
          String path = req.substring(space1 + 1, space2);

          if (path.startsWith("/telemetry"))
          {
            String json = "{";
            json += "\"ch0Shown\":" + String(chan[0].shown ? "true" : "false") + ",";
            json += "\"ch1Shown\":" + String(chan[1].shown ? "true" : "false") + ",";
            json += "\"ch0Vdiv\":" + String(chan[0].y_index) + ",";
            json += "\"ch1Vdiv\":" + String(chan[1].y_index) + ",";
            json += "\"timebase\":" + String(x_index) + ",";
            int trig_sel = 0;
            if (trig != 0) trig_sel = (trig_ch == 0) ? trig : trig + 2;
            json += "\"trigMode\":" + String(trig_sel) + ",";
            json += "\"ch0Telemetry\":\"" + ch0_telemetry_str + "\",";
            json += "\"ch1Telemetry\":\"" + ch1_telemetry_str + "\",";
            json += "\"ch0Freq\":" + String(chan[0].freq) + ",";
            json += "\"ch1Freq\":" + String(chan[1].freq) + ",";
            json += "\"ch0Vpp\":" + String(chan[0].v_max - chan[0].v_min) + ",";
            json += "\"ch1Vpp\":" + String(chan[1].v_max - chan[1].v_min) + ",";
            json += "\"ch0Vmin\":" + String(chan[0].v_min) + ",";
            json += "\"ch1Vmin\":" + String(chan[1].v_min) + ",";
            json += "\"ch0Vmax\":" + String(chan[0].v_max) + ",";
            json += "\"ch1Vmax\":" + String(chan[1].v_max) + ",";
            json += "\"fw\":\"" + String(FIRMWARE_VERSION) + "\"";
            json += "}";

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println("Connection: close");
            client.println();
            client.print(json);
          }
          else if (path.startsWith("/set"))
          {
            int val;

            val = getQueryVal(path, "ch0Shown");
            if (val != -1)
            {
              bool new_shown = (val != 0);
              if (chan[0].shown != new_shown)
              {
                chan[0].shown = new_shown;
                if (new_shown)
                {
                  lv_obj_add_state(btn_ch0, LV_STATE_CHECKED);
                  lv_obj_set_style_bg_color(btn_ch0, lv_color_hex(chan[0].color), LV_PART_MAIN);
                  lv_obj_set_style_text_color(btn_ch0, lv_color_black(), LV_PART_MAIN);
                }
                else
                {
                  lv_obj_clear_state(btn_ch0, LV_STATE_CHECKED);
                  lv_obj_set_style_bg_color(btn_ch0, lv_color_hex(0x333333), LV_PART_MAIN);
                  lv_obj_set_style_text_color(btn_ch0, lv_color_hex(0x888888), LV_PART_MAIN);
                }
                lv_obj_invalidate(scope_viewport);
                Serial.print("WiFi Remote: CH0 status set to: ");
                Serial.println(new_shown ? "ENABLED" : "DISABLED");
              }
            }

            val = getQueryVal(path, "ch1Shown");
            if (val != -1)
            {
              bool new_shown = (val != 0);
              if (chan[1].shown != new_shown)
              {
                chan[1].shown = new_shown;
                if (new_shown)
                {
                  lv_obj_add_state(btn_ch1, LV_STATE_CHECKED);
                  lv_obj_set_style_bg_color(btn_ch1, lv_color_hex(chan[1].color), LV_PART_MAIN);
                  lv_obj_set_style_text_color(btn_ch1, lv_color_black(), LV_PART_MAIN);
                }
                else
                {
                  lv_obj_clear_state(btn_ch1, LV_STATE_CHECKED);
                  lv_obj_set_style_bg_color(btn_ch1, lv_color_hex(0x333333), LV_PART_MAIN);
                  lv_obj_set_style_text_color(btn_ch1, lv_color_hex(0x888888), LV_PART_MAIN);
                }
                lv_obj_invalidate(scope_viewport);
                Serial.print("WiFi Remote: CH1 status set to: ");
                Serial.println(new_shown ? "ENABLED" : "DISABLED");
              }
            }

            val = getQueryVal(path, "ch0Vdiv");
            if (val != -1 && val < VOLTS_MAX && chan[0].y_index != val)
            {
              chan[0].y_index = val;
              int r = voltage[val].range_idx;
              digitalWrite(chan[0].pin1, (r >> 1));
              digitalWrite(chan[0].pin0, (r & 1));
              lv_dropdown_set_selected(dd_ch0_vdiv, val);
              lv_obj_invalidate(scope_viewport);

              char v_str[16];
              ch_vdiv_str(val, v_str);
              Serial.print("WiFi Remote: CH0 Volts/Div set to: ");
              Serial.print(v_str); Serial.print(" (AFE range: "); Serial.print(r); Serial.println(")");
            }

            val = getQueryVal(path, "ch1Vdiv");
            if (val != -1 && val < VOLTS_MAX && chan[1].y_index != val)
            {
              chan[1].y_index = val;
              int r = voltage[val].range_idx;
              digitalWrite(chan[1].pin1, (r >> 1));
              digitalWrite(chan[1].pin0, (r & 1));
              lv_dropdown_set_selected(dd_ch1_vdiv, val);
              lv_obj_invalidate(scope_viewport);

              char v_str[16];
              ch_vdiv_str(val, v_str);
              Serial.print("WiFi Remote: CH1 Volts/Div set to: ");
              Serial.print(v_str); Serial.print(" (AFE range: "); Serial.print(r); Serial.println(")");
            }

            val = getQueryVal(path, "timebase");
            if (val != -1 && val < TB_MAX && x_index != val)
            {
              x_index = val;
              adc.stop();
              adc.begin(ADC_RESOLUTION, tb[x_index].sps, N_SAMPLES, 32);
              lv_dropdown_set_selected(dd_tb, x_index);
              lv_obj_invalidate(scope_viewport);

              char t_str[16];
              tb_tdiv_str(x_index, t_str);
              char sps_str[16];
              tb_sps_str(x_index, sps_str);
              Serial.print("WiFi Remote: Timebase set to: "); Serial.print(t_str);
              Serial.print(" "); Serial.println(sps_str);
            }

            val = getQueryVal(path, "trigMode");
            if (val != -1 && val <= 4)
            {
              int new_trig_ch = 0;
              if (val == 3 || val == 4) new_trig_ch = 1;
              int y_ind = chan[new_trig_ch].y_index;
              int r = voltage[y_ind].range_idx;

              switch (val)
              {
                case 0:
                  trig = 0;
                  trig_level = range[r].rising_level;
                  break;
                case 1:
                  trig = 1; trig_ch = 0;
                  trig_level = range[r].rising_level;
                  break;
                case 2:
                  trig = 2; trig_ch = 0;
                  trig_level = range[r].falling_level;
                  break;
                case 3:
                  trig = 1; trig_ch = 1;
                  trig_level = range[r].rising_level;
                  break;
                case 4:
                  trig = 2; trig_ch = 1;
                  trig_level = range[r].falling_level;
                  break;
              }

              lv_dropdown_set_selected(dd_trig, val);
              lv_obj_invalidate(scope_viewport);

              Serial.print("WiFi Remote: Trigger set to: ");
              if (trig == 0)
              {
                Serial.println("OFF");
              }
              else
              {
                Serial.print((trig == 1) ? "Rising" : "Falling");
                Serial.print(" CH"); Serial.print(trig_ch);
                Serial.print(", Level: "); Serial.print(trig_level); Serial.println("V");
              }
            }

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            client.print("OK");
          }
          else if (path == "/" || path == "/index.html" || path == "/duinoscope_control_panel.html")
          {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html; charset=utf-8");
            client.println("Connection: close");
            client.println();
            client.print(html_page);
          }
          else
          {

            client.println("HTTP/1.1 302 Found");
            client.println("Location: http://" + String(apIP[0]) + "." + String(apIP[1]) + "." + String(apIP[2]) + "." + String(apIP[3]) + "/");
            client.println("Connection: close");
            client.println();
          }
        }
      }
      client.stop();
    }
  }

  if (adc.available())
  {
    SampleBuffer buf = adc.read();

    if ((millis() - last_millis) > 100)
    {

      sample_count = buf.size();
      for (int i = 0; i < sample_count && i < 2 * N_SAMPLES; i++)
      {
        sample_data[i] = buf[i];
      }

      find_trig_and_freq(sample_data, sample_count, 0);
      find_trig_and_freq(sample_data, sample_count, 1);
      process_buffer(sample_data, sample_count);

      if (trig != 0)
        x_offset = -(chan[trig_ch].trig_pt / 2) * tb[x_index].p_sam;
      else
        x_offset = 0;

      update_labels();

      uint32_t now = millis();

      for (int ch = 0; ch < 2; ch++)
      {
        if (chan[ch].shown)
        {
          float freq = chan[ch].freq;
          float vpp = chan[ch].v_max - chan[ch].v_min;

          bool has_signal = (freq >= 1.0f);
          bool had_signal = (last_printed_shown[ch] && (last_printed_freq[ch] >= 1.0f));
          bool first_report = !last_printed_shown[ch];

          bool should_print = false;

          if (first_report)
          {
            should_print = true;
          }
          else if (has_signal != had_signal)
          {

            should_print = true;
          }
          else if (has_signal)
          {

            bool sig_freq_diff = (fabsf(freq - last_printed_freq[ch]) / last_printed_freq[ch] >= 0.05f);
            bool sig_vpp_diff = (last_printed_vpp[ch] > 0.0f && fabsf(vpp - last_printed_vpp[ch]) / last_printed_vpp[ch] >= 0.10f);
            bool heartbeat = (now - last_telemetry_time[ch] >= 5000);

            if (sig_freq_diff || sig_vpp_diff || heartbeat)
            {
              should_print = true;
            }
          }

            if (should_print)
            {
              Serial.print("Telemetry CH"); Serial.print(ch); Serial.print(": ");
              String tel_str;
              if (!has_signal)
              {
                char v_str[16];
                sprintf(v_str, "%.2fV", chan[ch].v_min);
                Serial.print("Flat/No Signal (Vdc: ");
                Serial.print(chan[ch].v_min);
                Serial.println("V)");
                tel_str = "Flat (" + String(v_str) + ")";
              }
              else
              {
                char f_str[16];
                ch_freq_str(ch, f_str);
                Serial.print("Signal Detected! Freq: ");
                Serial.print(f_str);
                Serial.print(", Vp-p: ");
                Serial.print(vpp);
                Serial.println("V");

                char vpp_str[16];
                sprintf(vpp_str, "%.2fVp-p", vpp);
                tel_str = String(f_str) + " " + String(vpp_str);
              }

              if (ch == 0) ch0_telemetry_str = tel_str;
              else ch1_telemetry_str = tel_str;

              last_printed_freq[ch] = freq;
              last_printed_vpp[ch] = vpp;
              last_printed_shown[ch] = true;
              last_telemetry_time[ch] = now;
            }
          }
          else
          {

            if (last_printed_shown[ch])
            {
              Serial.print("Telemetry CH"); Serial.print(ch); Serial.println(": DISABLED");
              last_printed_shown[ch] = false;
              last_printed_freq[ch] = 0.0f;
              last_printed_vpp[ch] = 0.0f;

              if (ch == 0) ch0_telemetry_str = "Disabled";
              else ch1_telemetry_str = "Disabled";
            }
          }
        }

      lv_obj_invalidate(scope_viewport);

      bool ch0_active = false;
      bool ch1_active = false;

      for (int ch = 0; ch < 2; ch++)
      {
        if (chan[ch].shown)
        {
          float freq = chan[ch].freq;
          float vpp = chan[ch].v_max - chan[ch].v_min;
          int y_ind = chan[ch].y_index;
          int r = voltage[y_ind].range_idx;
          float v_range = V_RANGE(r);
          float v_min_limit = V_MIN(r);

          if ((freq >= 1.0f) ||
              (vpp > 0.05f * v_range) ||
              (fabsf(chan[ch].v_min - v_min_limit) > 0.05f * v_range))
          {
            if (ch == 0) ch0_active = true;
            if (ch == 1) ch1_active = true;
          }
        }
      }

      if (!ch0_active && !ch1_active)
      {

        setLEDs(false, true, false, 10);
      }

      else
      {
        static bool blink_state = false;
        blink_state = !blink_state;

        if (ch0_active && ch1_active)
        {
          if (blink_state)
          {

            setLEDs(true, true, false);
          }
          else
          {

            setLEDs(false, true, true);
          }
        }
        else if (ch0_active)
        {
          if (blink_state)
          {

            setLEDs(true, true, false);
          }
          else
          {
            setLEDs(false, false, false);
          }
        }
        else
        {
          if (blink_state)
          {

            setLEDs(false, true, true);
          }
          else
          {
            setLEDs(false, false, false);
          }
        }
      }

      last_millis = millis();
    }

    buf.release();
  }

  updateBoardLEDs();

  delay(5);
}
