#include <stddef.h>	// *nix compatibility
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "SDL2/SDL.h"		// 2.3MiB
//#include "SDL2/SDL_image.h"
#include "SDL2/SDL_ttf.h"	// 4.8MiB


#include "mmu.h"	// ROM reading, memory init.
#include "core.h"	// core stepping
#include "lcd.h"	// LCD simulation
#include "memmap.h"

#include "SFR/sfr.h"
#include "SFR/standby.h"
#include "SFR/interrupt.h"
#include "SFR/timer.h"
#include "SFR/keyboard.h"


//#define IMG_TYPES (IMG_INIT_JPG | IMG_INIT_PNG)
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 640

// SimU8 configs
#define TIMER_DEVIDER 1

#define MAX_CYCLE_PER_FRAME 5000000		// 5M * 60fps = 300MHz

//#define EMULATE_CWII
//#define EMULATE_CWI
#define EMULATE_ESP


#ifdef EMULATE_CWII
	#define SAVE_FILE_NAME "state_cwii.sav"
	#define ROM_FILE_NAME "rom_cwii.bin"
	#define CHECKSUM_ADDR 0	// placeholder
	#define CYCLE_PER_FRAME (2097152/60)	// CWII? - 2MHz HSCLK
#endif
#ifdef EMULATE_CWI
	#define SAVE_FILE_NAME "state_cwi.sav"
	#define ROM_FILE_NAME "rom_cwi.bin"
	#define CHECKSUM_ADDR 0x222d4	// 991CNX C
	#define CYCLE_PER_FRAME (1048576/60)	// CWI - 1MHz HSCLK
#endif
#ifdef EMULATE_ESP
	#define SAVE_FILE_NAME "state_esp.sav"
	#define ROM_FILE_NAME "rom_esp.bin"
	#define CHECKSUM_ADDR 0x04a9e	// 570ESP E
	#define CYCLE_PER_FRAME (524088/60)	// ES+ - 512kHz HSCLK
#endif

#define KEY_IDLE 0
#define KEY_PRESSED 1
#define KEY_HOLD 3

#define KEYBOARD_X 0
#define KEYBOARD_Y (LCD_BASE_Y + LCD_HEIGHT * LCD_PIX_HEIGHT + 16)

#define KEY_WIDTH 32
#define KEY_HEIGHT 20

#define KEYBOARD_GAP_WIDTH 8
#define KEYBOARD_GAP_HEIGHT 8

// used in `processKeys`
// indicates mouse motion only
#define KEYBOARD_MOVING -2

#define KEY_COLOR_IDLE 180, 180, 192, 255
#define KEY_COLOR_HOVERED 192, 208, 208, 255
#define KEY_COLOR_PRESSED 132, 132, 132, 255
#define KEY_COLOR_HOLD 128, 64, 64, 255

// upper left corner of LCD
#define LCD_BASE_X 0
#define LCD_BASE_Y 16
#if defined(EMULATE_CWI) || defined(EMULATE_CWII)
// LCD pixel dimensions
	#define LCD_PIX_WIDTH 2
	#define LCD_PIX_HEIGHT 2
// LCD pixel color, in RGBA order
// CWI/CWII LCD color (FSTN: black-on-gray)
	#define LCD_PIX_COLOR_SET 64, 30, 64, 255
	#define LCD_PIX_COLOR_CLEAR 216, 225, 204, 255
#endif
#ifdef EMULATE_ESP
// LCD pixel dimensions
	#define LCD_PIX_WIDTH 3
	#define LCD_PIX_HEIGHT 3
// ES+ LCD color (STN: blue-on-green)
	#define LCD_PIX_COLOR_SET 127, 100, 180, 255
	#define LCD_PIX_COLOR_CLEAR 192, 204, 180, 255
#endif

// background color
#define SDL_BG_COLOR 152, 192, 168, 255


typedef struct {
	uint16_t x;	// key coordinates
	uint16_t y;
	uint16_t w;	// key sizes
	uint16_t h;
	char *text;	// text/name of the key
} SimU8_key_t;


typedef enum {
	SIMU8_CORE_RUNNING,
	SIMU8_CORE_WAITING,
	SIMU8_CORE_SINGLESTEP,
	SIMU8_CORE_ONEFRAME,
	SIMU8_CORE_TERMINATING
} SimU8_Core_Status_t;


// SDL contexts
static SDL_Window *MainWindow = NULL;
static SDL_Renderer *MainRenderer = NULL;
static SDL_Thread *CoreThread = NULL;
static SDL_mutex *CoreStatusMutex = NULL;

// SDL_ttf contexts
static TTF_Font *MainFont = NULL;
#define TTF_FONT_NAME "font.ttf"
#define TTF_FONT_SIZE 16
#define TTF_FONT_COLOR ((SDL_Color){32, 48, 48, 255})
#define TTF_FONT_BG ((SDL_Color){SDL_BG_COLOR})


static int _lcd_offset_x = 0;
static int _lcd_offset_y = 0;

// SimU8 variables
static volatile SimU8_Core_Status_t CoreIsDebugging = SIMU8_CORE_RUNNING;
static volatile int TimerDivider = 0;
static int Keyboard[8][8] = {0};	// row is KIL, column is KOL
static int CyclePerFrame = CYCLE_PER_FRAME;	// for speed adjustment

// -------- LCD
// CWII code
#ifdef EMULATE_CWII
volatile uint8_t LCDBuffer[256/8*64*2];

void renderCWIIBuffer(uint8_t *buffer, unsigned short bufferWidth, unsigned short bufferHeight) {
	SDL_Rect rect;
	int row, col, bit, color;
	const volatile uint8_t * bp0 = buffer;
	const volatile uint8_t * bp1 = buffer + (bufferWidth / 8) * bufferHeight;	// bitplane pointers
	uint8_t sh0, sh1;

	rect.w = LCD_PIX_WIDTH;
	rect.h = LCD_PIX_HEIGHT;

	for( row = 0; row < LCD_HEIGHT; ++row ) {
		rect.x = LCD_BASE_X + _lcd_offset_x;	// reset X
		rect.y = LCD_BASE_Y + _lcd_offset_y + LCD_PIX_HEIGHT * row;
		for( col = 0; col < LCD_WIDTH / 8; ++col ) {
			// fetch a byte
			sh0 = *bp0++;
			sh1 = *bp1++;
			for( bit = 0; bit < 8; ++bit ) {
				color = 4 - ((sh0 & 0x80)? 1 : 0) - ((sh1 & 0x80)? 2 : 0);
				sh0 <<= 1; sh1 <<= 1;
				SDL_SetRenderDrawColor(MainRenderer, 255 / 4 * color, 255 / 4 * color, 255 / 4 * color, 255);
				SDL_RenderFillRect(MainRenderer, &rect);
				rect.x += LCD_PIX_WIDTH;
			}
		}
		// advance pointers
		bp0 += (bufferWidth - LCD_WIDTH) / 8;
		bp1 += (bufferWidth - LCD_WIDTH) / 8;
	}
}

// empty function
void setPix(int x, int y, int c) {
	;
}

void updateDisp(void) {
/*
	SDL_Rect rect;
	int row, col, bit, color;
	const volatile uint8_t * bp0 = LCDBuffer;
	const volatile uint8_t * bp1 = LCDBuffer + 256/8*64;	// bitplane pointers
	uint8_t sh0, sh1;
//	uint8_t *bp0 = DataMemory + 0xd654 - ROM_WINDOW_SIZE, *bp1;
//	bp1 = bp0;

	rect.w = LCD_PIX_WIDTH - 1;
	rect.h = LCD_PIX_HEIGHT - 1;

	for( row = 0; row < LCD_HEIGHT; ++row ) {
		rect.x = LCD_BASE_X;	// reset X
		rect.y = LCD_BASE_Y + LCD_PIX_HEIGHT * row;
		for( col = 0; col < LCD_WIDTH / 8; ++col ) {
			// fetch a byte
			sh0 = *bp0++;
			sh1 = *bp1++;
			for( bit = 0; bit < 8; ++bit ) {
				color = 4 - ((sh0 & 0x80)? 1 : 0) - ((sh1 & 0x80)? 2 : 0);
				sh0 <<= 1; sh1 <<= 1;
				SDL_SetRenderDrawColor(MainRenderer, 255 / 4 * color, 255 / 4 * color, 255 / 4 * color, 255);
				SDL_RenderFillRect(MainRenderer, &rect);
				rect.x += LCD_PIX_WIDTH;
			}
		}
		// advance pointers
		bp0 += (VRAM_WIDTH - LCD_WIDTH) / 8;
		bp1 += (VRAM_WIDTH - LCD_WIDTH) / 8;
	}
*/
	renderCWIIBuffer(LCDBuffer, 256, 64);
}
#else
// monochrome emulation
void setPix(int x, int y, int c) {
	SDL_Rect rect;

	rect.x = LCD_BASE_X + LCD_PIX_WIDTH * x + _lcd_offset_x;
	rect.y = LCD_BASE_Y + LCD_PIX_HEIGHT * y + _lcd_offset_y;
//	rect.w = LCD_PIX_WIDTH - 1;
//	rect.h = LCD_PIX_HEIGHT - 1;
	rect.w = LCD_PIX_WIDTH;
	rect.h = LCD_PIX_HEIGHT;

	if( c ) {
		SDL_SetRenderDrawColor(MainRenderer, LCD_PIX_COLOR_SET);
	}
	else {
		SDL_SetRenderDrawColor(MainRenderer, LCD_PIX_COLOR_CLEAR);
	}
	SDL_RenderFillRect(MainRenderer, &rect);
}

// Empty function, not used
void updateDisp() {
	;
}
#endif

// move and put the string
// use `snprintf` if need formatted string
void _mvputs(int x, int y, const char* str) {
	SDL_Rect dest;
	SDL_Texture *fontTexture = NULL;
	SDL_Surface *fontSurface = NULL;

	dest.x = x;
	dest.y = y;
	// render text to surface (CPU side)
//	fontSurface = TTF_RenderUTF8_Solid_Wrapped(MainFont, str, TTF_FONT_COLOR, 0);	// 8-bit fast
	fontSurface = TTF_RenderUTF8_Shaded_Wrapped(MainFont, str, TTF_FONT_COLOR, TTF_FONT_BG, 0);	// 8-bit high quality
	// get text surface size
	dest.w = fontSurface -> w;
	dest.h = fontSurface -> h;
	// convert the text surface to texture (GPU side)
	fontTexture = SDL_CreateTextureFromSurface(MainRenderer, fontSurface);
	SDL_FreeSurface(fontSurface);
	// copy to renderer
	SDL_RenderCopy(MainRenderer, fontTexture, NULL, &dest);
	SDL_DestroyTexture(fontTexture);
}


// keyboard emulation
uint16_t getKI(uint16_t maskedKO) {
	int i, j, pass;
	uint8_t KIs[8] = {0}, KOs[8];
	uint16_t KI = 0;

//	printf("[getKI] KO = %04Xh", maskedKO);

	if( maskedKO == 0 ) {
//		putchar('\n');
		return 0xFFFF;
	}

	// translate bit pattern into array
	for( i = 0; i < 8; ++i ) {
		KOs[i] = maskedKO & 1;
		maskedKO >>= 1;
		KIs[i] = 0;	// clear KIs
	}

	// do 8 passes, to account for every single key on the key matrix
	for( pass = 0; pass < 8; ++pass ) {
		// set KIs based on KOs
		for( i = 0; i < 8; ++i ) {
			if( !KOs[i] )
				continue;
			// else KO is asserted
			for( j = 0; j < 8; ++j ) {
				if( Keyboard[j][i] >= KEY_PRESSED ) {
					KIs[j] |= 1;
				}
			}
		}
		// set KOs based on KIs
		for( j = 0; j < 8; ++j ) {
			if( !KIs[j] )
				continue;
			// else KI is asserted(low)
			for( i = 0; i < 8; ++i ) {
				if( Keyboard[j][i] >= KEY_PRESSED ) {
					KOs[i] |= 1;
				}
			}
		}
	}

	// translate KIs back to bit pattern
	// bit order is reversed
	for( i = 0; i < 8; ++i ) {
		KI = (KI << 1) | (KIs[i]? 1 : 0);
	}

//	printf(", KI = %02Xh.\n", KI);

	return ~KI | 0xFF00;	// KIH defaults to `disabled`
}

static void processKeys(int mouseX, int mouseY, int mouseButton) {
	int x, y, row, col, key, status;
	SDL_Rect rect;
	for( row = 0; row < 8; ++row ) {
		for( col = 0; col < 8; ++col ) {
			// get key status
			key = Keyboard[row][col];
			// calculate rect position & bounding box(?)
			x = KEYBOARD_X + (KEY_WIDTH + KEYBOARD_GAP_WIDTH) * col;
			y = KEYBOARD_Y + (KEY_HEIGHT + KEYBOARD_GAP_HEIGHT) * row;
			rect.x = x;
			rect.y = y;
			rect.w = KEY_WIDTH;
			rect.h = KEY_HEIGHT;
			// set render color based on key state
			switch( key ) {
				case KEY_IDLE:
					SDL_SetRenderDrawColor(MainRenderer, KEY_COLOR_IDLE);
					break;
				case KEY_PRESSED:
					SDL_SetRenderDrawColor(MainRenderer, KEY_COLOR_PRESSED);
					break;
				case KEY_HOLD:
					SDL_SetRenderDrawColor(MainRenderer, KEY_COLOR_HOLD);
					break;
				default:
					break;
			}
			// detect hovering (override key state)
			if( mouseX >= x && mouseX < (x + KEY_WIDTH) && mouseY >= y && mouseY <= (y + KEY_HEIGHT) ) {
				// hovered
				SDL_SetRenderDrawColor(MainRenderer, KEY_COLOR_HOVERED);
				switch( mouseButton ) {
					case SDL_BUTTON_LEFT:
						// left mouse button - press
						if( key != KEY_HOLD )
							key = KEY_PRESSED;
						SDL_SetRenderDrawColor(MainRenderer, KEY_COLOR_PRESSED);
						break;
					case SDL_BUTTON_RIGHT:
						// right mouse button - toggle hold
						if( key == KEY_HOLD ) {
							key = KEY_IDLE;
						}
						else {
							key = KEY_HOLD;
						}
						break;
					case KEYBOARD_MOVING:
					default:
						if( key != KEY_HOLD )
							key = KEY_IDLE;
						break;
				}
			}
			else {
				// not hovered
				if( key == KEY_PRESSED )
					key = KEY_IDLE;
			}
			Keyboard[row][col] = key;
			SDL_RenderFillRect(MainRenderer, &rect);
		}
	}
}


#define MEMDUMP_BYTE_PER_ROW 16

void _dumpMemory(uint32_t base, uint32_t count) {
	unsigned char hexStr[MEMDUMP_BYTE_PER_ROW * 4 + 3];
	unsigned int col;
	uint8_t byte;

	hexStr[MEMDUMP_BYTE_PER_ROW * 4 + 2] = '\0';
	while( count != 0 ) {
		// reset buffer
		memset(hexStr, ' ', sizeof(hexStr) - 1);	// clear buffer to spaces
//		hexStr[MEMDUMP_BYTE_PER_ROW * 3] = '|';
//		hexStr[MEMDUMP_BYTE_PER_ROW * 3 + 1] = ' ';

		printf("%06Xh:\t", base);

		// read bytes
		for( col = 0; ; ++col ) {
			byte = (uint8_t)memoryGetData(base >> 16, base & 0xFFFF, 1);
			++base; --count;
			sprintf(hexStr + 3 * col, "%02X ", byte);
			hexStr[MEMDUMP_BYTE_PER_ROW * 3 + 2 + col] = ((byte >= 32) && (byte <= 127))? byte : '.';

			if( (col >= (MEMDUMP_BYTE_PER_ROW - 1)) || (count == 0) ) {
				// print current line
				hexStr[(col + 1) * 3] = ' ';
				hexStr[MEMDUMP_BYTE_PER_ROW * 3] = '|';
				puts(hexStr);
				break;
			}
		}
	}
}

void _pokeMemory(uint32_t base, uint32_t count) {
	uint8_t byte;

	printf("[_pokeMemory] Paste/Type %d 2-digit hexadecimals here,\n\tThey'll be stored to %06Xh.\n\tI hope you didn't make any mistake there LOL\n", count, base);
	while( count != 0 ) {
		if( scanf("%02X", &byte) != 1) {
			printf("[_pokeMemory] Unable to read in byte!\n\t%d bytes remaining, please type them again: ", count);
//			fflush(stdin);
			while( getchar() != '\n' )
				;
			continue;
		}
		memorySetData(base >> 16, base & 0xFFFF, 1, byte);	// non-reentrant??
		printf("\t%06Xh <-- %02Xh\n", base, byte);
//		_dumpMemory(base, count);
		++base; --count;
	}
}


// -------- core thread
// This thread will run until the end of application
int coreThreadHandler(void *data) {
	unsigned long cycles = 0;
	CORE_STATUS s;

	while( 1 ) {
		// check to pause the core on every frame
//		SDL_LockMutex(CoreStatusMutex);
		if( CoreIsDebugging == SIMU8_CORE_TERMINATING ) {
			break;
		}
//		SDL_UnlockMutex(CoreStatusMutex);
//		printf("Bap!\n");
		cycles = 0;
		while( cycles < CyclePerFrame ) {
			if( (StandbyState == STANDBY_NONE) && (CoreIsDebugging != SIMU8_CORE_WAITING) ) {
				s = coreStep();
				cycles += CycleCount;

				switch( s ) {
					case CORE_ILLEGAL_INSTRUCTION:
					// set single step mode
						SDL_LockMutex(CoreStatusMutex);
//						if( CoreIsDebugging == SIMU8_CORE_RUNNING ) {
							printf("[main] !!! Illegal instruction %04X at %01X:%04X !!! \n", memoryGetCodeWord(CSR, PC-2), CSR, PC);
//						}
						CoreIsDebugging = SIMU8_CORE_WAITING;
						SDL_UnlockMutex(CoreStatusMutex);
						break;

					default:
						break;
				}
			}
			else {	// core is sleeping/waiting for debugger
			 	//puts("[coreThread] STOP mode / waiting for debugger");
//				break;	// well no because that will make the core check for interrupts only 60 times per second
				SDL_Delay(2);
			}
			if( CoreIsDebugging == SIMU8_CORE_SINGLESTEP ) {
				CoreIsDebugging = SIMU8_CORE_WAITING;
				break;
			}
			if( CoreIsDebugging == SIMU8_CORE_WAITING ) {
				break;
			}
			if( CoreIsDebugging == SIMU8_CORE_TERMINATING ) {
				break;
			}
		}

		if( CoreIsDebugging == SIMU8_CORE_RUNNING ) {
			checkKeyboardInterrupt();
			checkTimerInterrupt();

			// handle interrupts
			switch( handleInterrupt() ) {
				case TIMER_INT_INDEX:
					cleanTimerIRQ();
					break;
				case KEYBOARD_INT_INDEX:
					cleanKeyboardIRQ();
					break;
				default:
					break;
			}
		}
		SDL_Delay(16);	// ~60FPS
	}
	puts("[coreThreadHandler] Exiting thread!");
	return 0;
}

// -------- peripheral callback
// This is periodically called by SDL_timer
Uint32 periphHandler(Uint32 interval, void *param) {
//	SDL_LockMutex(CoreStatusMutex);
	if( CoreIsDebugging != SIMU8_CORE_TERMINATING ) {
		updateKeyboard();
		if( ++TimerDivider >= TIMER_DEVIDER ) {
			updateTimer();
			TimerDivider = 0;
		}
//		SDL_UnlockMutex(CoreStatusMutex);
		return interval;
	}
//	SDL_UnlockMutex(CoreStatusMutex);
	puts("[periphHandler] Cancelling schedule!");
	return 0;	// cancel the schedule
}


int main(int argc, char *argv[]) {
	int retVal = 0;
	int mouseButton = 0, mouseX = 0, mouseY = 0;
	SDL_Event event;
	SDL_Keycode key;

	FILE *saveFile;
	uint32_t pokeAddr = 0, pokeLength = 0;
	uint32_t inspectAddr = 0;

// Initialize SDL2
	if( (retVal = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER)) != 0 ) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL unable to initialize!", SDL_GetError(), NULL);
		return retVal;
	}

/*
// Initialize SDL_image
	if( (retVal = IMG_Init(IMG_TYPES)) != IMG_TYPES) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL_image unable to initialize!", IMG_GetError(), NULL);
		SDL_Quit();
		return retVal;
	}
*/

// Initialize SDL_ttf
	if( (retVal = TTF_Init()) != 0 ) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL_ttf unable to initialize!", TTF_GetError(), NULL);
		SDL_Quit();
		return retVal;
	}

// Open font
	if( (MainFont = TTF_OpenFont(TTF_FONT_NAME, TTF_FONT_SIZE)) == NULL ) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL_ttf failed to load font!", TTF_GetError(), NULL);
		TTF_Quit();
		SDL_Quit();
		return retVal;
	}

// Create window and renderer
	if( (retVal = SDL_CreateWindowAndRenderer(\
		WINDOW_WIDTH, WINDOW_HEIGHT, \
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE, \
		&MainWindow, &MainRenderer)) != 0 ) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unable to create window/renderer!", SDL_GetError(), NULL);
//		IMG_Quit();
		TTF_CloseFont(MainFont);
		TTF_Quit();
		SDL_Quit();
		return retVal;
	}

	if( (CoreStatusMutex = SDL_CreateMutex()) == NULL ) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to create mutex", SDL_GetError(), NULL);
		exit(15);
	}

// SimU8 initialization
	if( memoryInit(ROM_FILE_NAME, NULL) != MEMORY_OK ) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to init memory", "`memoryInit` reported an error.\nCheck if \'"ROM_FILE_NAME"\' exists.", NULL);
		retVal = -16;
		goto sdlexit;
	}
	initTimer();
	initKeyboard();
	exitStandby();
	coreReset();
	// create core thread
	CoreThread = SDL_CreateThread(coreThreadHandler, "SU8Core", NULL);
	// run peripheral task
	SDL_AddTimer(1, periphHandler, NULL);	// improve keyboard responsiveness

	SDL_SetWindowTitle(MainWindow, "SimU8 SDL2 frontend");

//	SDL_SetRenderDrawColor(MainRenderer, SDL_BG_COLOR);
//	SDL_RenderClear(MainRenderer);


// main game loop
	while( 1 ) {
		SDL_SetRenderDrawColor(MainRenderer, SDL_BG_COLOR);
		SDL_RenderClear(MainRenderer);

//		static char Title[257];
//		sprintf(Title, "SimU8 SDL2 frontend - Cycle = %d, CSR:PC = %01X:%04Xh | %s", CycleCount, CSR, PC, (CoreIsDebugging == SIMU8_CORE_RUNNING)? "Running" : "Debugging");
//		SDL_SetWindowTitle(MainWindow, Title);

		static char formatBuffer[1024];
		snprintf(formatBuffer, \
			1023, \
			"Cycle = %d (%s)\n"\
			"Last code words = %04X %04X\n"\
			" CSR:PC:     %01X:%04Xh  PSW:   %02Xh\n"\
			" LCSR:LR:    %01X:%04Xh\n"\
			" ECSR1:ELR1: %01X:%04Xh  EPSW1: %02Xh\n"\
			" ECSR2:ELR2: %01X:%04Xh  EPSW2: %02Xh\n"\
			" ECSR3:ELR3: %01X:%04Xh  EPSW3: %02Xh\n"\
			"XR0:  %08Xh\n"\
			"XR4:  %08Xh\n"\
			"XR8:  %08Xh\n"\
			"XR12: %08Xh\n"\
			" DSR:EA:  %02X:%04Xh\n"\
			" SP:         %04Xh\n"\
			, \
			CycleCount, (CoreIsDebugging == SIMU8_CORE_RUNNING)? "Running" : "Debugging", \
			memoryGetCodeWord(CSR, PC - 4), memoryGetCodeWord(CSR, PC - 2), \
			CSR, PC, PSW.raw, \
			LCSR, LR, ECSR1, ELR1, EPSW1.raw, ECSR2, ELR2, EPSW2.raw, ECSR3, ELR3, EPSW3.raw, \
			GR.xrs[0], GR.xrs[1], GR.xrs[2], GR.xrs[3], \
			DSR, EA, \
			SP
		);
		_mvputs(0, 380, formatBuffer);

//		SDL_SetWindowTitle(MainWindow, "SimU8 SDL2 frontend");

		if( SDL_PollEvent(&event) == 1 ) {
			if( event.type == SDL_QUIT ) {
				// quit application
				// lower priority than keyboard/window events
				break;
			}

			if( event.type == SDL_WINDOWEVENT ) {
				if( event.window.event == SDL_WINDOWEVENT_CLOSE ) {
					// Alt+F4, clicking "X"
					SDL_Delay(500);
					break;
				}
			}

			if( event.type == SDL_KEYDOWN ) {
				key = event.key.keysym.sym;
				if( key == SDLK_q || key == SDLK_ESCAPE ) {
					SDL_Delay(100);
					break;
				}
			}

			// Input handling
			switch( event.type ) {
				case SDL_MOUSEBUTTONDOWN:
					mouseButton = event.button.button;
/*					switch( mouseButton ) {
						case SDL_BUTTON_LEFT:
						// LMB down
						break;

						case SDL_BUTTON_RIGHT:
						// RMB down
						break;
					}
*/					//processKeys(event.motion.x, event.motion.y, mouseButton);
					break;
/*
				case SDL_MOUSEBUTTONUP:
					mouseButton = event.button.button;
					break;
*/
				case SDL_MOUSEMOTION:
					mouseX = event.motion.x;
					mouseY  = event.motion.y;
					mouseButton = KEYBOARD_MOVING;
					SDL_FlushEvent(SDL_MOUSEMOTION);	// temporary fix
					break;

				case SDL_KEYDOWN:
					key = event.key.keysym.sym;
					switch( key ) {
						case SDLK_c:
						// Clear/reset core
							puts("\n[main] core reset.");
							SDL_LockMutex(CoreStatusMutex);
							coreZero();	// zero all registers
							coreReset();
							printf("[main] CSR:PC reset to %01X:%04Xh\n", CSR, PC);
							exitStandby();
							CoreIsDebugging = SIMU8_CORE_RUNNING;
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_t:
						// Force checksum calculation
							puts("\n[main] jumping to checksum function...");
							SDL_LockMutex(CoreStatusMutex);
							CSR = CHECKSUM_ADDR >> 16;
							PC = CHECKSUM_ADDR & 0xFFFF;
							printf("[main] CSR:PC reset to %01X:%04Xh\n", CSR, PC);
							exitStandby();
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_w:
						// save state
							SDL_LockMutex(CoreStatusMutex);
							puts("\n[main] Saving state to "SAVE_FILE_NAME"...");
							if( (saveFile = fopen(SAVE_FILE_NAME, "wb")) == NULL ) {
								puts("[main] !!! Failed to create save file !!!");
							}
							else {
								fwrite(DataMemory, sizeof(uint8_t), 0x10000 - ROM_WINDOW_SIZE, saveFile);
								fwrite(&CoreRegister, sizeof(CoreRegister), 1, saveFile);
								fclose(saveFile);
								puts("[main] Savestate done.");
							}
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_e:
						// load state
							SDL_LockMutex(CoreStatusMutex);
							puts("\n[main] Reading state from "SAVE_FILE_NAME"...");
							if( (saveFile = fopen(SAVE_FILE_NAME, "rb")) == NULL ) {
								puts("[main] !!! Failed to read from save file !!!");
							}
							else {
								fread(DataMemory, sizeof(uint8_t), 0x10000 - ROM_WINDOW_SIZE, saveFile);
								fread(&CoreRegister, sizeof(CoreRegister), 1, saveFile);
								fclose(saveFile);
								puts("[main] Savestate loaded.");
							}
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_m:
						// inspect memory
							SDL_LockMutex(CoreStatusMutex);
							CoreIsDebugging = SIMU8_CORE_SINGLESTEP;	// I don't know
							// TODO: implement some lock step thing
							//	So main thread can control core thread to single-step
							printf("\n[main] Inspect memory...\n\tData memory base address (%04Xh) is %p.\nInput a 6-digit data memory address: ", ROM_WINDOW_SIZE, DataMemory);
							fflush(stdin);
							if( scanf("%06X", &inspectAddr) != 1 ) {
								puts("\nFailed to read address...");
							}
							else {
								_dumpMemory(inspectAddr, 128);
							}
							CoreIsDebugging = SIMU8_CORE_RUNNING;
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_p:
						// poke memory
							SDL_LockMutex(CoreStatusMutex);
							CoreIsDebugging = SIMU8_CORE_WAITING;	// I don't know
							// TODO: implement some lock step thing
							//	So main thread can control core thread to single-step
							//memorySetData(0, 0x8154, 1, 0xFF);	// test
							printf("\n[main] Poke memory...\nInput `address(6-digit HEX), size(decimal)` to poke: ");
							fflush(stdin);
							if( scanf("%06X,%d", &pokeAddr, &pokeLength) != 2 ) {
								puts("\nFailed to read parameters...");
							}
							else {
								_pokeMemory(pokeAddr, pokeLength);
								//_dumpMemory(pokeAddr, pokeLength);
							}
							CoreIsDebugging = SIMU8_CORE_RUNNING;
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_r:
						// resume
							puts("\n[main] Resuming...");
							SDL_LockMutex(CoreStatusMutex);
							CoreIsDebugging = SIMU8_CORE_RUNNING;
							exitStandby();
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_s:
						// single-step
							puts("\n[main] Single-stepping...");
							SDL_LockMutex(CoreStatusMutex);
							CoreIsDebugging = SIMU8_CORE_SINGLESTEP;
							SDL_UnlockMutex(CoreStatusMutex);
							break;
						case SDLK_j:
						// slowing down
							if( (CyclePerFrame >> 3) > 8 )
								CyclePerFrame -= CyclePerFrame >> 3;	// 15/16
							else
								CyclePerFrame -= 1;
							if( CyclePerFrame <= 0 )
								CyclePerFrame = 1;
							printf("\n[main] Current cycle per second = %d\n", CyclePerFrame * 60);
							break;
						case SDLK_k:
						// speeding up
							if( (CyclePerFrame >> 3) > 8 )
								CyclePerFrame += CyclePerFrame >> 3;	// 17/16
							else
								CyclePerFrame += 1;
							if( CyclePerFrame > MAX_CYCLE_PER_FRAME ) {
								CyclePerFrame = MAX_CYCLE_PER_FRAME;
								puts("\n[main] Warning: Maximum core speed reached!");
							}
							printf("\n[main] Current cycle per second = %d\n", CyclePerFrame * 60);
							break;
						case SDLK_l:
						// restore speed
							CyclePerFrame = CYCLE_PER_FRAME;
							printf("\n[main] Current cycle per second = %d\n", CyclePerFrame * 60);
							break;
						default:
							break;
					}
					break;

				default:
					mouseButton = KEYBOARD_MOVING;
					break;
			}

		}

		// render keyboard
		processKeys(mouseX, mouseY, mouseButton);

		// frame updating
		_lcd_offset_x = 0; _lcd_offset_y = 0;
		renderVRAM();
#ifdef EMULATE_ESP
		_lcd_offset_x = LCD_WIDTH * LCD_PIX_WIDTH + 16;
		renderBuffer(DataMemory + 0x87D0 - ROM_WINDOW_SIZE);
		// temporary - show memory around stack
		uint8_t *mem = DataMemory;
		for( int i = 0; i < 2; ++i ) {
			_lcd_offset_y = LCD_HEIGHT * LCD_PIX_HEIGHT + 16;
			for( int j = 0; j < 5; ++j ) {
				renderBuffer(mem);
				mem += 96*32/8;
				_lcd_offset_y += LCD_HEIGHT * LCD_PIX_HEIGHT;
			}
			_lcd_offset_x += LCD_WIDTH * LCD_PIX_WIDTH + 16;
		}
/*		_lcd_offset_y = LCD_HEIGHT * LCD_PIX_HEIGHT + 16;
		renderBuffer(DataMemory + 0x8DA4 - 96*32/8*4 - ROM_WINDOW_SIZE);
		_lcd_offset_y += LCD_HEIGHT * LCD_PIX_HEIGHT;
		renderBuffer(DataMemory + 0x8DA4 - 96*32/8*3 - ROM_WINDOW_SIZE);
		_lcd_offset_y += LCD_HEIGHT * LCD_PIX_HEIGHT;
		renderBuffer(DataMemory + 0x8DA4 - 96*32/8*2 - ROM_WINDOW_SIZE);
		_lcd_offset_y += LCD_HEIGHT * LCD_PIX_HEIGHT;
		renderBuffer(DataMemory + 0x8DA4 - 96*32/8 - ROM_WINDOW_SIZE);
*/
#endif
#ifdef EMULATE_CWI
		_lcd_offset_x = LCD_WIDTH * LCD_PIX_WIDTH + 16;
		renderBuffer(DataMemory + 0xddd4 - ROM_WINDOW_SIZE);
		_lcd_offset_y = LCD_HEIGHT * LCD_PIX_HEIGHT + 16;
		renderBuffer(DataMemory + 0xe3d4 - ROM_WINDOW_SIZE);
#endif
#ifdef EMULATE_CWII
		_lcd_offset_x = LCD_WIDTH * LCD_PIX_WIDTH + 16;
		renderCWIIBuffer(DataMemory + 0xca54 - ROM_WINDOW_SIZE, 192, 64);
		_lcd_offset_y = LCD_HEIGHT * LCD_PIX_HEIGHT + 16;
		renderCWIIBuffer(DataMemory + 0xd654 - ROM_WINDOW_SIZE, 192, 64);
#endif

		// presenting
		SDL_RenderPresent(MainRenderer);
		SDL_Delay(17);	// 17ms ~= 1/60s
//		SDL_Delay(1);	// 5ms = 1/1000s
	}

	// terminate core thread
	SDL_LockMutex(CoreStatusMutex);
	CoreIsDebugging = SIMU8_CORE_TERMINATING;
	SDL_UnlockMutex(CoreStatusMutex);
	puts("[main] Termination signal sent!");
	SDL_WaitThread(CoreThread, NULL);
	puts("[main] Core thread joined.");
	memoryFree();

sdlexit:
	SDL_DestroyRenderer(MainRenderer);
	SDL_DestroyWindow(MainWindow);
//	IMG_Quit();
	TTF_CloseFont(MainFont);
	TTF_Quit();
	SDL_Quit();

	puts("[main] Exiting...");
	return retVal;
}
