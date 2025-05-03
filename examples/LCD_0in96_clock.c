#include "LCD_Test.h"
#include "LCD_0in96.h"
#include "string.h"
#include "pico/util/datetime.h"
#include "ds3231.h"

extern char buf[];
extern char *week[];

static UWORD *BlackImage = NULL;

void lcd_clock_init(void) {
    DEV_Delay_ms(100);
    printf("Initializing LCD Clock...\r\n");

    if (DEV_Module_Init() != 0) {
        printf("Failed to initialize LCD module.\r\n");
        return;
    }

    LCD_0IN96_Init(HORIZONTAL);
    LCD_0IN96_Clear(WHITE);

    UDOUBLE Imagesize = LCD_0IN96_HEIGHT * LCD_0IN96_WIDTH * 2;

    if ((BlackImage = (UWORD *)malloc(Imagesize)) == NULL) {
        printf("Failed to allocate memory for LCD...\r\n");
        exit(0);
    }

    Paint_NewImage((UBYTE *)BlackImage, LCD_0IN96.WIDTH, LCD_0IN96.HEIGHT, 0, WHITE);
    Paint_SetScale(65);
    Paint_SetRotate(ROTATE_0);
    Paint_Clear(WHITE);

    DEV_SET_PWM(100);  // Max backlight
}

void lcd_clock_update(void) {
    ds3231ReadTime();

    buf[0] = buf[0] & 0x7F; // sec
    buf[1] = buf[1] & 0x7F; // min
    buf[2] = buf[2] & 0x3F; // hour
    buf[3] = buf[3] & 0x07; // week
    buf[4] = buf[4] & 0x3F; // day
    buf[5] = buf[5] & 0x1F; // month

    if (buf[6] == 0x00 && buf[5] == 0x01 && buf[4] == 0x01) {
        ds3231SetTime();
    }

    char text[64] = " Datetime: ";
    strcat(text, week[(unsigned char)buf[3] - 1]);

    // Make a datetime string
    char datetime_str[32];
    snprintf(datetime_str, sizeof(datetime_str), "%02x/%02x/%02x %02x:%02x:%02x",
             buf[6], buf[5], buf[4], buf[2], buf[1], buf[0]);

    int y_pos = (LCD_0IN96.HEIGHT - Font12.Height) / 2;

    Paint_Clear(WHITE);
    Paint_DrawString_EN(10, y_pos, &text[0], &Font12, BLACK, WHITE);
    Paint_DrawString_EN(20, y_pos + 12, &datetime_str[0], &Font12, BLACK, WHITE);
    LCD_0IN96_Display(BlackImage);
}

void lcd_clock_deinit(void) {
    if (BlackImage != NULL) {
        free(BlackImage);
        BlackImage = NULL;
    }
    DEV_Module_Exit();
}