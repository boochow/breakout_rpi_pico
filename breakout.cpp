#include <string.h>
#include <cstdlib>
#include "pico.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/regs/rosc.h"

// PWM sound generator

#define PWM_AUDIO_L    (27)
#define PWM_AUDIO_R    (28)
#define RANDOMBIT      (*((uint *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET)) & 1)

#define PWM_RANGE_BITS (10)
#define PWM_RANGE      (1<<PWM_RANGE_BITS)
#define NUM_PSG        (4)
#define VOL_MAX        (PWM_RANGE / NUM_PSG - 1)
#define SAMPLE_RATE    (125000000 / PWM_RANGE)
#define OMEGA_UNIT     (FIXED_1_0 / SAMPLE_RATE)

#define FIXED_0_5      (0x40000000)
#define FIXED_1_0      (0x7fffffff)

typedef uint32_t fixed; // 1.0=0x7fffffff, 0.0=0x0

enum psg_type {OSC_SQUARE, OSC_SAW, OSC_TRI, OSC_NOISE};
		 
struct psg_t {
    volatile fixed phase;       // 0..FIXED_1_0
    fixed step;                 // 0..FIXED_1_0
    volatile int sound_vol;     // 0..VOL_MAX
    enum psg_type type;
};

static struct psg_t psg[NUM_PSG];

void psg_freq(int i, float freq) {
    assert(i < NUM_PSG);
    psg[i].step = freq * OMEGA_UNIT; 
}

void psg_vol(int i, int value) {
    assert(i < NUM_PSG);
    if (value < 0) {
	value = 0;
    }
    psg[i].sound_vol = value % (VOL_MAX + 1);
}

void psg_type(int i, enum psg_type type) {
    assert(i < NUM_PSG);
    psg[i].type = type;
}

static inline uint psg_value(int i) {
    assert(i < NUM_PSG);
    uint result;
    if (psg[i].type == OSC_SQUARE) {
	result = (psg[i].phase > FIXED_0_5) ? psg[i].sound_vol : 0;
    } else if (psg[i].type == OSC_SAW) {
	result = ((psg[i].phase >> (31 - PWM_RANGE_BITS)) * psg[i].sound_vol) >> PWM_RANGE_BITS;
    } else if (psg[i].type == OSC_TRI) {
	result = ((psg[i].phase >> (30 - PWM_RANGE_BITS)) * psg[i].sound_vol) >> PWM_RANGE_BITS;
	result = (result < psg[i].sound_vol) ? result : psg[i].sound_vol * 2 - result;
    } else { // OSC_NOISE
	result = RANDOMBIT * psg[i].sound_vol;
    }
    return result;
}

static inline void psg_next() {
    for(int i = 0; i < NUM_PSG; i++) {
	psg[i].phase += psg[i].step;
	if (psg[i].phase > FIXED_1_0) {
	    psg[i].phase -= FIXED_1_0;
	}
    }
}

void on_pwm_wrap() {
    uint sum = 0;
    
    pwm_clear_irq(pwm_gpio_to_slice_num(PWM_AUDIO_L));
#ifdef PWM_AUDIO_R
    pwm_clear_irq(pwm_gpio_to_slice_num(PWM_AUDIO_R));
#endif
    psg_next();
    for(int i = 0; i < NUM_PSG; i++) {
	sum += psg_value(i);
    }
    pwm_set_gpio_level(PWM_AUDIO_L, sum);
#ifdef PWM_AUDIO_R
    pwm_set_gpio_level(PWM_AUDIO_R, sum);
#endif
}

void psg_pwm_config() {
    gpio_set_function(PWM_AUDIO_L, GPIO_FUNC_PWM);
#ifdef PWM_AUDIO_R
    gpio_set_function(PWM_AUDIO_R, GPIO_FUNC_PWM);
#endif
    uint slice_num = pwm_gpio_to_slice_num(PWM_AUDIO_L);
    pwm_clear_irq(slice_num);
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 1);
    pwm_config_set_wrap(&config, PWM_RANGE);
    pwm_init(slice_num, &config, true);
#ifdef PWM_AUDIO_R
    slice_num = pwm_gpio_to_slice_num(PWM_AUDIO_R);
    pwm_init(slice_num, &config, true);
#endif
}

void psg_init() {
    for(int i = 0; i < NUM_PSG; i++) {
	psg[i].phase = 0;
	psg[i].step = 0;
	psg[i].sound_vol = VOL_MAX / 4;
	psg[i].type = OSC_SQUARE;
    }
    psg_pwm_config();    
}


// scanning for the button states
// this code is from pico-playground/apps/popcorn

#define BUTTON_LEFT 1
#define BUTTON_MIDDLE 2
#define BUTTON_RIGHT 4

volatile uint32_t button_state = 0;
static const uint button_pins[] = {0, 6, 11};

const uint VSYNC_PIN = PICO_SCANVIDEO_COLOR_PIN_BASE + PICO_SCANVIDEO_COLOR_PIN_COUNT + 1;

// set pins to input. On deassertion, sample and set back to output.
void vga_board_button_irq_handler() {
    int vsync_current_level = gpio_get(VSYNC_PIN);
    gpio_acknowledge_irq(VSYNC_PIN, vsync_current_level ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL);

    // Note v_sync_polarity == 1 means active-low because anything else would be confusing
    if (vsync_current_level != scanvideo_get_mode().default_timing->v_sync_polarity) {
        for (int i = 0; i < count_of(button_pins); ++i) {
            gpio_pull_down(button_pins[i]);
            gpio_set_oeover(button_pins[i], GPIO_OVERRIDE_LOW);
        }
    } else {
        uint32_t state = 0;
        for (int i = 0; i < count_of(button_pins); ++i) {
            state |= gpio_get(button_pins[i]) << i;
            gpio_set_oeover(button_pins[i], GPIO_OVERRIDE_NORMAL);
        }
        button_state = state;
    }
}

void vga_board_init_buttons() {
    gpio_set_irq_enabled(VSYNC_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    irq_set_exclusive_handler(IO_IRQ_BANK0, vga_board_button_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

// the game logic

const uint16_t black     = PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, 0, 0);
const uint16_t rp_leaf   = PICO_SCANVIDEO_PIXEL_FROM_RGB8(107,192,72);
const uint16_t rp_berry  = PICO_SCANVIDEO_PIXEL_FROM_RGB8(196,25,73);
const uint16_t colors[]  = {black, black, rp_leaf, rp_berry};

constexpr int screen_width = 33;
constexpr int screen_height = 60;
constexpr int to_screen = 4;

constexpr int racket_size = 8;
constexpr int racket_line = 55;

constexpr int brick_width = 3;
constexpr int brick_height = 2;

constexpr int bricks_top = 3;
constexpr int bricks_bottom = 39;
constexpr int pi_logo[] = {0,0,1,1,1,0,1,1,1,0,0,
			   0,1,2,2,2,1,2,2,2,1,0,
			   0,1,2,1,2,1,2,1,2,1,0,
			   0,1,2,2,1,1,1,2,2,1,0,
			   0,0,1,2,1,1,1,2,1,0,0,
			   0,0,1,1,1,3,1,1,1,0,0,
			   0,1,3,3,1,3,1,3,3,1,0,
			   0,1,3,1,1,3,1,1,3,1,0,
			   0,1,1,3,3,1,3,3,1,1,0,
			   1,3,1,3,3,1,3,3,1,3,1,
			   1,3,1,3,3,1,3,3,1,3,1,
			   1,3,3,1,1,1,1,1,3,3,1,
			   1,3,1,1,3,3,3,1,1,3,1,
			   0,1,3,1,3,3,3,1,3,1,0,
			   0,1,3,3,1,3,1,3,3,1,0,
			   0,0,1,3,1,1,1,3,1,0,0,
			   0,0,1,1,3,3,3,1,1,0,0,
			   0,0,0,0,1,1,1,0,0,0,0};
constexpr int bricks_rows = screen_width / brick_width;
constexpr int bricks_lines = (bricks_bottom - bricks_top + 1) / brick_height;
constexpr int num_bricks = screen_width / brick_width * bricks_lines;
constexpr int no_brick = -1;

struct ball {
    uint8_t x, y;
    int8_t vx, vy;
};
  
struct brick {
    uint8_t  x;
    uint8_t  y;
    uint16_t pen;
    bool     exists;
};

enum game_status {
    Game_Restart,
    Game_OnGoing,
};

brick bricks[num_bricks];

struct ball ball;
int16_t racket = (screen_width - racket_size) / 2;
game_status game_status;

void sound_on(float freq) {
    psg_freq(0, freq);
    psg_freq(1, freq * 1.016);
    psg_vol(0, VOL_MAX);
    psg_vol(1, VOL_MAX);
}

void sound_decay() {
    if (psg[0].sound_vol > 0) {
	int v = psg[0].sound_vol * .85;
	psg_vol(0, v);
	psg_vol(1, v);
    }
}

int16_t find_brick(uint8_t x, uint8_t y) {
    int16_t result = no_brick;
    if ((x <= screen_width) && (y >= bricks_top) && (y <= bricks_bottom)) {
	for(int i = 0; i < num_bricks ; i++) {
	    int dx = x - bricks[i].x;
	    int dy = y - bricks[i].y;
	    if (0 <= dx && dx < brick_width && 0 <= dy && dy < brick_height) {
		result = i;
		break;
	    }
	}
    }
    return result;
}

int16_t hit_brick(uint8_t x, uint8_t y, int8_t* vx, int8_t* vy) {
    int16_t result;
  
    result = find_brick(x + *vx, y);
    if ((result >= 0) && bricks[result].exists) {
	*vx = -*vx;
	return result;
    }

    result = find_brick(x, y + *vy);
    if ((result >= 0) && bricks[result].exists) {
	*vy = -*vy;
	return result;
    }

    result = find_brick(x + *vx, y + *vy);
    if ((result >= 0) && bricks[result].exists) {
	*vx = -*vx;
	*vy = -*vy;
	return result;
    }

    return no_brick;
}

void init_bricks() {
    for(int i = 0; i < bricks_lines; i++) {
	for(int j = 0; j < bricks_rows; j++) {
	    int b = i * bricks_rows + j;
	    bricks[b].x = j * brick_width;
	    bricks[b].y = i * brick_height + bricks_top;
	    int p = pi_logo[b];
	    bricks[b].pen = colors[p];
	    if (p == 0) {
		bricks[b].exists = false;
	    } else {
		bricks[b].exists = true;
	    }
	}
    }
}

uint16_t left_bricks() {
    uint16_t result = 0;
    for(int i = 0; i < num_bricks ; i++) {
	if (bricks[i].exists) {
	    result++;
	}
    }
    return result;
}

void init_ball() {
    ball.x = screen_width -1;
    ball.y = bricks_bottom + brick_height;
    ball.vx = -1;
    ball.vy = 1;
}

void init_game() {
    game_status = Game_Restart;
    init_bricks();
    init_ball();
}

bool restart_game() {
    if ((ball.y > bricks_bottom) && ball.vy > 0) {
	init_bricks();
	return true;
    } else {
	return false;
    }
}

bool process_ball() {
    int16_t brick_test;
    bool hit = false;

    do {
	brick_test = hit_brick(ball.x, ball.y, &ball.vx, &ball.vy);
	if (brick_test != no_brick) {
	    bricks[brick_test].exists = false;
	    hit = true;
	}
    } while (brick_test != no_brick);
    return hit;
}

void update_ball() {
    ball.x += ball.vx;
    if (ball.x >= screen_width - 1 || ball.x < 1) {
	ball.vx *= -1;
    }

    ball.y += ball.vy;
    if (ball.y == 0) {
	ball.vy *= -1;
    } else if (ball.y == racket_line) {
	if (ball.x >= racket && ball.x < racket + racket_size) {
	    ball.vy *= -1;
	    sound_on(220);
	} 
    } else if (ball.y > racket_line) {
	if (ball.y > screen_height) {
	    init_ball();
	} else {
	    sound_on(110);
	}
    }
}

void move_racket() {
    if (button_state & BUTTON_RIGHT) {
	racket += 2;
	if (racket > screen_width - racket_size) {
	    racket = screen_width - racket_size;
	}
    } else if (button_state & BUTTON_LEFT) {
	racket -= racket > 2 ? 2 : racket;
    }

    if (button_state & BUTTON_MIDDLE) {
	racket = ball.x - racket_size / 2;
	if (racket > screen_width - racket_size) {
	    racket = screen_width - racket_size;
	} else if (racket < 0) {
	    racket = 0;
	}
    }
}

void update_game() {
    switch(game_status) {
    case Game_Restart:
	if (restart_game()) {
	    game_status = Game_OnGoing;
	}
	break;
    case Game_OnGoing:
	if (process_ball()) {
	    sound_on(440);
	}
	if (!left_bricks()) {
	    game_status = Game_Restart;
	}
	break;
    }

    update_ball();
    move_racket();
}

// scanline video renderer

#define VGA_MODE vga_mode_320x240_60
#define MIN_RUN 3

const uint16_t white     = PICO_SCANVIDEO_PIXEL_FROM_RGB8(255, 255, 255);
const uint16_t dark_grey = PICO_SCANVIDEO_PIXEL_FROM_RGB8(20, 40, 60);
const uint16_t yellow    = PICO_SCANVIDEO_PIXEL_FROM_RGB8(255, 255, 0);
const uint16_t dark_blue = PICO_SCANVIDEO_PIXEL_FROM_RGB8(0, 0, 40);

const uint16_t bg_color  = dark_grey;

const uint16_t racket_color = white | PICO_SCANVIDEO_ALPHA_MASK;
const uint16_t ball_color  = yellow | PICO_SCANVIDEO_ALPHA_MASK;

constexpr uint16_t brick_wpxls = brick_width * to_screen - 1;

int32_t inline rle_no_brick(uint32_t *buf) {
    constexpr uint16_t no_brick_w = brick_wpxls * (bricks_rows + 1);

    buf[0] = COMPOSABLE_COLOR_RUN     | (bg_color << 16);
    buf[1] = no_brick_w - 1 - MIN_RUN | (COMPOSABLE_RAW_1P_SKIP_ALIGN << 16);
    buf[2] = bg_color;              //| -- the last token is ignored --
    return 3;
}

int32_t inline rle_bricks(uint32_t *buf, brick *brick) {
    for(int i = 0; i < bricks_rows; i++, brick++) {
	uint16_t color = brick->exists ? brick->pen : bg_color;
	buf[0] = COMPOSABLE_COLOR_RUN  | (color << 16);
	buf[1] = brick_wpxls - MIN_RUN | (COMPOSABLE_RAW_1P_SKIP_ALIGN << 16);
	buf[2] = color;              //| -- the last token is ignored --
	buf += 3;
    }
    return 3 * bricks_rows;
}

int32_t scanline_bricks(uint32_t *buf, size_t buf_length, int line_num) {
    assert(buf_length >= 6 + 3 * bricks_rows);
    buf[0] = COMPOSABLE_COLOR_RUN  | (dark_blue << 16);
    buf[1] = 93 - MIN_RUN          | (COMPOSABLE_RAW_1P_SKIP_ALIGN << 16);
    buf[2] = dark_blue;          //| -- the last token is ignored --
    buf += 3;
    
    int y = line_num / to_screen;
    int data_used = 0;
    if ((y >= bricks_top) && (y < bricks_bottom)) {
	int offset = (y - bricks_top) / brick_height * bricks_rows;
	data_used = rle_bricks(buf, bricks + offset);
    } else {
	data_used = rle_no_brick(buf);
    }
    
    buf[data_used] = COMPOSABLE_COLOR_RUN  | (dark_blue << 16);
    buf[data_used + 1] = 93 - MIN_RUN      | (COMPOSABLE_RAW_1P << 16);
    buf[data_used + 2] = 0                 | (COMPOSABLE_EOL_ALIGN << 16);

    return data_used + 6;
}

int32_t inline rle_hline(uint32_t *buf, uint16_t x, uint16_t w, uint16_t c) {
    buf[0] = COMPOSABLE_COLOR_RUN      | 0;
    buf[1] = x - MIN_RUN               | (COMPOSABLE_COLOR_RUN << 16);
    buf[2] = c                         | ((w - MIN_RUN) << 16);
    buf[3] = COMPOSABLE_RAW_1P         | 0;
    buf[4] = COMPOSABLE_EOL_SKIP_ALIGN;
    return 5;
}

int32_t inline rle_blank_line(uint32_t *buf) {
    buf[0] = COMPOSABLE_COLOR_RUN     | 0;
    buf[1] = VGA_MODE.width - MIN_RUN | (COMPOSABLE_EOL_ALIGN << 16);
    return 2;
}

int32_t scanline_racket(uint32_t *buf, size_t buf_len, int line_num) {
    uint16_t racket_x = 94 + racket * to_screen;
    uint16_t racket_width = racket_size * to_screen;
    int y = line_num - (racket_line * to_screen);

    if ((y >= 0) && (y < to_screen)) {
	return rle_hline(buf, racket_x, racket_width, racket_color);
    } else {
	return rle_blank_line(buf);
    }
}

int32_t scanline_ball(uint32_t *buf, size_t buf_len, int line_num) {
    uint16_t ball_x = 94 + ball.x * to_screen;
    uint16_t ball_width = to_screen;
    int y = line_num - (ball.y * to_screen);

    if ((y >= 0) && (y < to_screen)) {
	return rle_hline(buf, ball_x, ball_width, ball_color);
    } else {
	return rle_blank_line(buf);
    }
}

void single_scanline(struct scanvideo_scanline_buffer *dest) {
    uint32_t *buf = dest->data;
    size_t buf_length = dest->data_max;
    int line_num = scanvideo_scanline_number(dest->scanline_id);

    dest->data_used = scanline_bricks(dest->data, dest->data_max, line_num);
    dest->data2_used = scanline_racket(dest->data2, dest->data2_max, line_num);
    dest->data3_used = scanline_ball(dest->data3, dest->data3_max, line_num);
    
    dest->status = SCANLINE_OK;
}

void frame_update_logic(int num) {
    if ((num & 1) || (button_state & BUTTON_MIDDLE)) {
	update_game();
    }
    sound_decay();
}

// main loop

void render_loop() {
    static uint32_t last_frame_num = 0;

    while (true) {
        struct scanvideo_scanline_buffer *scanline_buffer = scanvideo_begin_scanline_generation(true);

        uint32_t frame_num = scanvideo_frame_number(scanline_buffer->scanline_id);
        if (frame_num != last_frame_num) {
            last_frame_num = frame_num;
	    frame_update_logic(frame_num);
        }
        single_scanline(scanline_buffer);
	
        scanvideo_end_scanline_generation(scanline_buffer);
    }
}

int main() {
    init_game();
    psg_init();
    vga_board_init_buttons();
    scanvideo_setup(&VGA_MODE);
    scanvideo_timing_enable(true);
    render_loop();
    return 0;
}
