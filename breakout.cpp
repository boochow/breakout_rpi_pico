#include <string.h>
#include <math.h>
#include <cstdlib>

#include "pico_display.hpp"

constexpr int screen_width = 33;
constexpr int screen_height = 60;
constexpr int to_screen = 4;

constexpr int racket_size = 8;
constexpr int racket_line = 57;

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

int16_t find_brick(uint8_t x, uint8_t y) {
  int16_t result = no_brick;
  if ((x <= screen_width) && (y >= bricks_top) && (y <= bricks_bottom)) {
    for(int i = 0; i < num_bricks ; i++) {
      int dx = x - bricks[i].x;
      int dy = y - bricks[i].y;
      if (0 <= dx && dx <=brick_width && 0 <= dy && dy <= brick_height) {
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

using namespace pimoroni;

uint16_t buffer[PicoDisplay::WIDTH * PicoDisplay::HEIGHT];
PicoDisplay pico_display(buffer);

const uint16_t white = pico_display.create_pen(255, 255, 255);
const uint16_t black = pico_display.create_pen(0, 0, 0);
const uint16_t dark_grey = pico_display.create_pen(20, 40, 60);
const uint16_t yellow = pico_display.create_pen(255, 255, 0);

const uint16_t rp_leaf = pico_display.create_pen(107,192,72);
const uint16_t rp_berry = pico_display.create_pen(196,25,73);
const uint16_t colors[] = {black, black, rp_leaf, rp_berry};

const uint16_t bg_color = dark_grey;

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

void draw_rect(Rect r) {
  Rect r2(r.y, PicoDisplay::HEIGHT - r.x - r.w, r.h, r.w);
  pico_display.rectangle(r2);
}

void draw_brick(brick b){
  if (b.exists) {
    pico_display.set_pen(b.pen);
  } else {
    pico_display.set_pen(bg_color);
  }
  Rect r(b.x * to_screen, b.y * to_screen, brick_width * to_screen, brick_height * to_screen);
  draw_rect(r);
}

void draw_ball(uint16_t color) {
  Rect r(ball.x * to_screen, ball.y * to_screen, 1 * to_screen, 1 * to_screen);
  pico_display.set_pen(color);
  draw_rect(r);
}

void draw_racket(uint16_t color) {
  Rect r(racket * to_screen, (racket_line + 1) * to_screen, racket_size * to_screen, 1 * to_screen);
  pico_display.set_pen(color);
  draw_rect(r);
}

void move_racket() {
  draw_racket(bg_color);
  if (pico_display.is_pressed(pico_display.X)) {
    racket += 2;
    if (racket > screen_width - racket_size) {
      racket = screen_width - racket_size;
    }
  } else
  if (pico_display.is_pressed(pico_display.Y)) {
    racket -= racket > 2 ? 2 : racket;
  } else
  if (pico_display.is_pressed(pico_display.B)) {
    racket = ball.x - racket_size / 2;
  }
  draw_racket(white);
}

void init_all() {
  pico_display.init();
  pico_display.set_backlight(70);
  pico_display.set_pen(bg_color);
  pico_display.clear();
  game_status = Game_Restart;
  init_bricks();
  init_ball();
}

bool restart_game() {
  if ((ball.y > bricks_bottom) && ball.vy > 0) {
    init_bricks();
    for(int i = 0 ; i < num_bricks ; i++) {
      draw_brick(bricks[i]);
    }
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
      draw_brick(bricks[brick_test]);
      hit = true;
    }
  } while (brick_test != no_brick);
  return hit;
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
      pico_display.set_led(10,20,60);
    } else {
      pico_display.set_led(0, 0, 0);
    }
    if (!left_bricks()) {
      game_status = Game_Restart;
    }
    break;
  }
}

void update_ball() {
    draw_ball(bg_color);

    ball.x += ball.vx;
    if (ball.x >= screen_width - 1 || ball.x < 1) {
      ball.vx *= -1;
    }

    ball.y += ball.vy;
    if (ball.y == racket_line) {
      if (ball.x >= racket && ball.x < racket + racket_size) {
	ball.vy *= -1;
	pico_display.set_led(30,30,30);
      } else {
	pico_display.set_led(0, 0, 0);
      }
    }

    if (ball.y == 0) {
      ball.vy *= -1;
    }

    if (ball.y > screen_height) {
      init_ball();
    }

    draw_ball(yellow);
}

int main() {
  init_all();

  while(true) {

    update_game();
      
    update_ball();
    move_racket();

    pico_display.update();

    if (!pico_display.is_pressed(pico_display.A)) {
      sleep_ms(32);
    }
  }
  return 0;
}
