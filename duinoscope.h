
#define PIX_DIV     80

#define TB_MAX      10

typedef struct Timebase
{
  float t_div;
  long  sps;
  int   p_sam;
} Timebase;

Timebase tb[TB_MAX] =
{

  { 10,     1000000,                  PIX_DIV / 10},
  { 20,     1000000,                  PIX_DIV / 20},
  { 50,     1000000 * PIX_DIV / 100,  2           },
  { 100,    1000000 * PIX_DIV / 100,  1           },
  { 200,    1000000 * PIX_DIV / 200,  1           },
  { 500,    1000000 * PIX_DIV / 500,  1           },
  { 1000,   1000000 * PIX_DIV / 1000, 1           },
  { 2000,   1000000 * PIX_DIV / 2000, 1           },
  { 5000,   1000000 * PIX_DIV / 5000, 1           },
  { 10000,  1000000 * PIX_DIV / 10000,1           }
};

int x_index = 6;

int x_offset = 0;

#define ADC_RESOLUTION AN_RESOLUTION_10
#define ADC_BITS      10
#define ADC_RANGE     (1 << ADC_BITS)

#define N_SAMPLES     2048

typedef struct Vrange
{
  float     v_min;
  float     v_max;
  int       sign_offset;
  float     rising_level;
  float     falling_level;
  float     level_hyst;
} Vrange;

Vrange range[4] =
{

  { -5.872f, 5.917f, 509,       2.5f,   0.8f,   0.15f },
  { -2.152f, 2.497f, 473,       1.25f,  0.4f,   0.075f },
  { -1.121f, 0.949f, 553,       0.5f,   0.2f,   0.03f },
  { -0.404f, 0.595f, 413,       0.25f,  0.1f,   0.02f }
};

#define V_MAX(r)        range[r].v_max
#define V_MIN(r)        range[r].v_min
#define V_RANGE(r)      (range[r].v_max - range[r].v_min)
#define SIGN_OFFSET(r)  range[r].sign_offset

#define VOLTS_MAX     5

typedef struct Voltage
{
  float v_div;
  float pix_count;
  int   range_idx;
} Voltage;

Voltage voltage[VOLTS_MAX] =
{

  { 0.1f,  (V_RANGE(3) * PIX_DIV) / (ADC_RANGE * 0.1f), 3},
  { 0.2f,  (V_RANGE(2) * PIX_DIV) / (ADC_RANGE * 0.2f), 2},
  { 0.5f,  (V_RANGE(1) * PIX_DIV) / (ADC_RANGE * 0.5f), 1},
  { 1.0f,  (V_RANGE(0) * PIX_DIV) / (ADC_RANGE * 1.0f), 0},
  { 2.0f,  (V_RANGE(0) * PIX_DIV) / (ADC_RANGE * 2.0f), 0}
};

#define CH_COLOR_0 0xFFCC00
#define CH_COLOR_1 0x00FFFF

typedef struct Channel
{
  int       y_index;
  int       y_offset;
  bool      shown;
  int       y_min;
  int       y_max;
  float     v_min;
  float     v_max;
  int       trig_pt;
  float     freq;
  uint32_t  color;
  int       pin0;
  int       pin1;
} Channel;

Channel chan[2] =
{
  {3, 360, true,  0, 0, 0, 0, 0, 0, CH_COLOR_0, 3, 4 },
  {3, 400, false, 0, 0, 0, 0, 0, 0, CH_COLOR_1, 5, 6 }
};

float trig_level = range[0].rising_level;
int trig = 0;
int trig_ch = 0;
int trig_x = 20;
