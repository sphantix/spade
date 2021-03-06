/*
 * File Name: keyboard.c
 * Author: Sphantix
 * Mail: hangxu@antiy.cn
 * Created Time: Tue 08 Aug 2017 09:40:33 AM CST
 */

#include "types.h"
#include "const.h"
#include "proto.h"
#include "keyboard.h"
#include "keymap.h"

static kb_input_t kb_input_buffer;

static int code_with_E0 = 0;
static int shift_l;            /* l shift state */
static int shift_r;            /* r shift state */
static int alt_l;              /* l alt state    */
static int alt_r;              /* r left state   */
static int ctrl_l;             /* l ctrl state   */
static int ctrl_r;             /* l ctrl state   */
static int caps_lock;          /* Caps Lock  */
static int num_lock;           /* Num Lock   */
static int scroll_lock;        /* Scroll Lock    */
static int column;

static int caps_lock;          /* Caps Lock */
static int num_lock;           /* Num Lock */
static int scroll_lock;        /* Scroll Lock */

static void kb_wait(void) {
    u8 kb_stat;
    do {
        kb_stat = in_byte(KB_CMD);
    } while (kb_stat & 0x02);
}

static void kb_ack(void) {
    u8 kb_read;
    do {
        kb_read = in_byte(KB_DATA);
    } while (kb_read != KB_ACK);
}

static void set_leds(void) {
    u8 leds = (caps_lock << 2) | (num_lock << 1) | scroll_lock;

    kb_wait();
    out_byte(KB_DATA, LED_CODE);
    kb_ack();

    kb_wait();
    out_byte(KB_DATA, leds);
    kb_ack();
}

void keyboard_handler(int irq) {
    /* disp_str("*"); */
    u8 scan_code = in_byte(KB_DATA);

    if (kb_input_buffer.count < KB_IN_BYTES) {
        *(kb_input_buffer.head) = scan_code;
        kb_input_buffer.head++;
        if (kb_input_buffer.head == kb_input_buffer.buf + KB_IN_BYTES) {
            kb_input_buffer.head = kb_input_buffer.buf;
        }
        kb_input_buffer.count++;
    }
}

void init_keyboard(void) {
    kb_input_buffer.count = 0;
    kb_input_buffer.head = kb_input_buffer.tail = kb_input_buffer.buf;

    shift_l = shift_r = 0;
    alt_l = alt_r = 0;
    ctrl_l = ctrl_r = 0;

    caps_lock = 0;
    num_lock = 1;
    scroll_lock = 0;

    set_leds();

    set_irq_handler(KEYBOARD_IRQ, keyboard_handler);
    enable_irq(KEYBOARD_IRQ);
}

static u8 get_byte_from_kbuf(void) {
    u8 scan_code;

    while (kb_input_buffer.count <= 0) {}

    disable_interrupt();
    scan_code = *(kb_input_buffer.tail);
    kb_input_buffer.tail++;
    if (kb_input_buffer.tail == kb_input_buffer.buf + KB_IN_BYTES) {
        kb_input_buffer.tail = kb_input_buffer.buf;
    }
    kb_input_buffer.count--;
    enable_interrupt();

    return scan_code;
}

void keyboard_read(tty_t* tty) {
    u8 scan_code;
    char output[2];
    int make; /* 1: make; 0: break */
    u32 key = 0;
    u32* key_row;

    if (kb_input_buffer.count > 0) {
        code_with_E0 = 0;

        scan_code = get_byte_from_kbuf();

        /* 下面开始解析扫描码 */
        if (scan_code == 0xE1) {
            int i;
            u8 pausebrk_scode[] = {0xE1, 0x1D, 0x45,
                                   0xE1, 0x9D, 0xC5};
            int is_pausebreak = 1;
            for (i = 1; i < 6; i++) {
                if (get_byte_from_kbuf() != pausebrk_scode[i]) {
                    is_pausebreak = 0;
                    break;
                }
            }

            if (is_pausebreak) {
                key = PAUSEBREAK;
            }
        } else if (scan_code == 0xE0) {
            scan_code = get_byte_from_kbuf();

            /* PrintScreen 被按下 */
            if (scan_code == 0x2A) {
                if (get_byte_from_kbuf() == 0xE0) {
                    if (get_byte_from_kbuf() == 0x37) {
                        key = PRINTSCREEN;
                        make = 1;
                    }
                }
            }

            /* PrintScreen 被释放 */
            if (scan_code == 0xB7) {
                if (get_byte_from_kbuf() == 0xE0) {
                    if (get_byte_from_kbuf() == 0xAA) {
                        key = PRINTSCREEN;
                        make = 0;
                    }
                }
            }

            /* 不是PrintScreen, 此时scan_code为0xE0紧跟的那个值 */
            if (key == 0) {
                code_with_E0 = 1;
            }
        }

        if ((key != PAUSEBREAK) && (key != PRINTSCREEN)) {
            /* 首先判断Make Code 还是 Break Code */
            make = (scan_code & FLAG_BREAK ? 0 : 1);

            key_row = &keymap[(scan_code & 0x7F) * MAP_COLS];

            column = 0;

			int caps = shift_l || shift_r;

			if (caps_lock) {
				if ((key_row[0] >= 'a') && (key_row[0] <= 'z')){
					caps = !caps;
				}
			}
			if (caps) {
				column = 1;
			}

            if (code_with_E0) {
                column = 2;
            }

            key = key_row[column];

            switch(key) {
            case SHIFT_L:
                shift_l = make;
                key = 0;
                break;
            case SHIFT_R:
                shift_r = make;
                key = 0;
                break;
            case CTRL_L:
                ctrl_l = make;
                key = 0;
                break;
            case CTRL_R:
                ctrl_r = make;
                key = 0;
                break;
            case ALT_L:
                alt_l = make;
                key = 0;
                break;
            case ALT_R:
                alt_l = make;
                key = 0;
                break;
            case CAPS_LOCK:
                if (make) {
                    caps_lock = !caps_lock;
                    set_leds();
                }
                break;
            case NUM_LOCK:
                if (make) {
                    num_lock = !num_lock;
                    set_leds();
                }
                break;
            case SCROLL_LOCK:
                if (make) {
                    scroll_lock = !scroll_lock;
                    set_leds();
                }
                break;
            default:
                if (!make) {	/* 如果是 Break Code */
                    key = 0;	/* 忽略之 */
                }
                break;
            }

            if (make) { /* 忽略 Break Code */
                int pad = 0;

                /* 首先处理小键盘 */
                if ((key >= PAD_SLASH) && (key <= PAD_9)) {
                    pad = 1;
                    switch(key) {
                    case PAD_SLASH:
                        key = '/';
                        break;
                    case PAD_STAR:
                        key = '*';
                        break;
                    case PAD_MINUS:
                        key = '-';
                        break;
                    case PAD_PLUS:
                        key = '+';
                        break;
                    case PAD_ENTER:
                        key = ENTER;
                        break;
                    default:
                        if (num_lock &&
                            (key >= PAD_0) &&
                            (key <= PAD_9)) {
                            key = key - PAD_0 + '0';
                        } else if (num_lock &&
                                 (key == PAD_DOT)) {
                            key = '.';
                        } else {
                            switch(key) {
                            case PAD_HOME:
                                key = HOME;
                                break;
                            case PAD_END:
                                key = END;
                                break;
                            case PAD_PAGEUP:
                                key = PAGEUP;
                                break;
                            case PAD_PAGEDOWN:
                                key = PAGEDOWN;
                                break;
                            case PAD_INS:
                                key = INSERT;
                                break;
                            case PAD_UP:
                                key = UP;
                                break;
                            case PAD_DOWN:
                                key = DOWN;
                                break;
                            case PAD_LEFT:
                                key = LEFT;
                                break;
                            case PAD_RIGHT:
                                key = RIGHT;
                                break;
                            case PAD_DOT:
                                key = DELETE;
                                break;
                            default:
                                break;
                            }
                        }
                        break;
                    }
                }

                key |= shift_l	? FLAG_SHIFT_L	: 0;
                key |= shift_r	? FLAG_SHIFT_R	: 0;
                key |= ctrl_l	? FLAG_CTRL_L	: 0;
                key |= ctrl_r	? FLAG_CTRL_R	: 0;
                key |= alt_l	? FLAG_ALT_L	: 0;
                key |= alt_r	? FLAG_ALT_R	: 0;
                key |= pad      ? FLAG_PAD      : 0;

                in_process(tty, key);
            }
        }
    }
}
