/* USER CODE BEGIN Header */
/*
 * Project : NOIR SENTINEL - STM32 RFID Access Control
 * MCU     : STM32F091RCTx
 * Modules : RC522 + TTP229 + SSD1306 OLED + Buzzer
 *
 * RC522 : SCK PA5, MISO PA6, MOSI PA7, CS PB6, RST PC7
 * OLED  : SCL PB8, SDA PB9, I2C address 0x3C
 * TTP229: KEY1 PA0, KEY2 PA1, KEY3 PA4, KEY4 PB0
 *         KEY5 PC1, KEY6 PC0, KEY7 PC2, KEY8 PC3
 * Buzzer: PA9
 * UART2 : TX PA2, RX PA3, 115200 baud
 *
 * DEMO:
 *   PIN = 1823
 *   KEY2 -> 1
 *   KEY8 -> 8
 *   KEY3 -> 2
 *   KEY4 -> 3
 *
 * WARNING:
 *   RFID_ACCEPT_ANY_CARD = 1 accepts any readable card.
 *   Demo only. Not suitable for a real security system.
 */
/* USER CODE END Header */

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* =========================================================
   PERIPHERAL HANDLES
   ========================================================= */

I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart2;

/* =========================================================
   FUNCTION DECLARATIONS
   ========================================================= */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

/* =========================================================
   APPLICATION CONFIGURATION
   ========================================================= */

#define OLED_ADDR              (0x3C << 1)
#define RFID_TIMEOUT_MS        100U

#define PASSWORD_LENGTH        4U
#define LOCKOUT_MS             10000U

/* Demo only: accepts any readable RFID card */
#define RFID_ACCEPT_ANY_CARD   1

/* =========================================================
   MFRC522 REGISTERS
   ========================================================= */

#define CommandReg             0x01
#define ComIrqReg              0x04
#define ErrorReg               0x06
#define FIFODataReg            0x09
#define FIFOLevelReg           0x0A
#define ControlReg             0x0C
#define BitFramingReg          0x0D
#define ModeReg                0x11
#define TxControlReg           0x14
#define TxASKReg               0x15
#define TModeReg               0x2A
#define TPrescalerReg          0x2B
#define TReloadRegH            0x2C
#define TReloadRegL            0x2D
#define VersionReg             0x37

#define PCD_Idle               0x00
#define PCD_Transceive         0x0C

#define PICC_REQA              0x26
#define PICC_ANTICOLL          0x93

/* =========================================================
   APPLICATION STATE
   ========================================================= */

static const char password[PASSWORD_LENGTH + 1] = "1823";

static char entered_password[PASSWORD_LENGTH + 1] = {0};

static uint8_t password_length = 0;
static uint8_t locked_out = 0;

static uint32_t lockout_start = 0;

/* Keypad: one event per touch */
static uint8_t key_was_down[8] = {0};

/* =========================================================
   UART DEBUG
   ========================================================= */

static void Debug_Print(const char *s)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)s,
        (uint16_t)strlen(s),
        100
    );
}

/* =========================================================
   BUZZER
   ========================================================= */

static void Buzzer_Set(uint8_t on)
{
    HAL_GPIO_WritePin(
        BUZZER_SIGNAL_GPIO_Port,
        BUZZER_SIGNAL_Pin,
        on ? GPIO_PIN_SET : GPIO_PIN_RESET
    );
}

static void Buzzer_Beep(uint32_t ms)
{
    Buzzer_Set(1);
    HAL_Delay(ms);
    Buzzer_Set(0);
}

static void Buzzer_Short(void)
{
    Buzzer_Beep(60);
}

static void Buzzer_Success(void)
{
    Buzzer_Beep(80);
    HAL_Delay(70);

    Buzzer_Beep(80);
}

static void Buzzer_Failed(void)
{
    for (uint8_t i = 0; i < 3; i++)
    {
        Buzzer_Beep(100);

        if (i < 2)
        {
            HAL_Delay(70);
        }
    }
}

/* =========================================================
   SSD1306 OLED DRIVER
   Minimal 5x7 ASCII font
   ========================================================= */

static uint8_t oled_buffer[128 * 8];

static HAL_StatusTypeDef OLED_Command(uint8_t cmd)
{
    uint8_t packet[2] = {0x00, cmd};

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_ADDR,
        packet,
        2,
        100
    );
}

static HAL_StatusTypeDef OLED_Data(uint8_t *data, uint16_t len)
{
    uint8_t packet[129];

    if (len > 128)
    {
        return HAL_ERROR;
    }

    packet[0] = 0x40;

    memcpy(&packet[1], data, len);

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_ADDR,
        packet,
        len + 1,
        100
    );
}

static void OLED_Init(void)
{
    HAL_Delay(100);

    OLED_Command(0xAE);

    OLED_Command(0xD5);
    OLED_Command(0x80);

    OLED_Command(0xA8);
    OLED_Command(0x3F);

    OLED_Command(0xD3);
    OLED_Command(0x00);

    OLED_Command(0x40);

    OLED_Command(0x8D);
    OLED_Command(0x14);

    OLED_Command(0x20);
    OLED_Command(0x00);

    OLED_Command(0xA1);
    OLED_Command(0xC8);

    OLED_Command(0xDA);
    OLED_Command(0x12);

    OLED_Command(0x81);
    OLED_Command(0x7F);

    OLED_Command(0xD9);
    OLED_Command(0xF1);

    OLED_Command(0xDB);
    OLED_Command(0x40);

    OLED_Command(0xA4);
    OLED_Command(0xA6);
    OLED_Command(0xAF);
}

static void OLED_Clear(void)
{
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

static void OLED_Update(void)
{
    for (uint8_t page = 0; page < 8; page++)
    {
        OLED_Command(0xB0 + page);
        OLED_Command(0x00);
        OLED_Command(0x10);

        OLED_Data(&oled_buffer[page * 128], 128);
    }
}

static void OLED_Glyph(char c, uint8_t g[5])
{
    static const uint8_t digits[10][5] =
    {
        {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
        {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
        {0x42,0x61,0x51,0x49,0x46}, /* 2 */
        {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
        {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
        {0x27,0x45,0x45,0x45,0x39}, /* 5 */
        {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
        {0x01,0x71,0x09,0x05,0x03}, /* 7 */
        {0x36,0x49,0x49,0x49,0x36}, /* 8 */
        {0x06,0x49,0x49,0x29,0x1E}  /* 9 */
    };

    switch (c)
    {
        case 'A':
        {
            uint8_t a[5] = {0x7E,0x11,0x11,0x11,0x7E};
            memcpy(g, a, 5);
            return;
        }

        case 'C':
        {
            uint8_t a[5] = {0x3E,0x41,0x41,0x41,0x22};
            memcpy(g, a, 5);
            return;
        }

        case 'D':
        {
            uint8_t a[5] = {0x7F,0x41,0x41,0x22,0x1C};
            memcpy(g, a, 5);
            return;
        }

        case 'E':
        {
            uint8_t a[5] = {0x7F,0x49,0x49,0x49,0x41};
            memcpy(g, a, 5);
            return;
        }

        case 'F':
        {
            uint8_t a[5] = {0x7F,0x09,0x09,0x09,0x01};
            memcpy(g, a, 5);
            return;
        }

        case 'I':
        {
            uint8_t a[5] = {0x00,0x41,0x7F,0x41,0x00};
            memcpy(g, a, 5);
            return;
        }

        case 'L':
        {
            uint8_t a[5] = {0x7F,0x40,0x40,0x40,0x40};
            memcpy(g, a, 5);
            return;
        }

        case 'N':
        {
            uint8_t a[5] = {0x7F,0x02,0x04,0x08,0x7F};
            memcpy(g, a, 5);
            return;
        }

        case 'O':
        {
            uint8_t a[5] = {0x3E,0x41,0x41,0x41,0x3E};
            memcpy(g, a, 5);
            return;
        }

        case 'P':
        {
            uint8_t a[5] = {0x7F,0x09,0x09,0x09,0x06};
            memcpy(g, a, 5);
            return;
        }

        case 'R':
        {
            uint8_t a[5] = {0x7F,0x09,0x19,0x29,0x46};
            memcpy(g, a, 5);
            return;
        }

        case 'S':
        {
            uint8_t a[5] = {0x46,0x49,0x49,0x49,0x31};
            memcpy(g, a, 5);
            return;
        }

        case 'T':
        {
            uint8_t a[5] = {0x01,0x01,0x7F,0x01,0x01};
            memcpy(g, a, 5);
            return;
        }

        case 'U':
        {
            uint8_t a[5] = {0x3F,0x40,0x40,0x40,0x3F};
            memcpy(g, a, 5);
            return;
        }

        case 'V':
        {
            uint8_t a[5] = {0x1F,0x20,0x40,0x20,0x1F};
            memcpy(g, a, 5);
            return;
        }

        case 'Y':
        {
            uint8_t a[5] = {0x07,0x08,0x70,0x08,0x07};
            memcpy(g, a, 5);
            return;
        }

        case '*':
        {
            uint8_t a[5] = {0x14,0x08,0x3E,0x08,0x14};
            memcpy(g, a, 5);
            return;
        }

        case '-':
        {
            uint8_t a[5] = {0x08,0x08,0x08,0x08,0x08};
            memcpy(g, a, 5);
            return;
        }

        case ' ':
        {
            memset(g, 0, 5);
            return;
        }

        default:
        {
            if (c >= '0' && c <= '9')
            {
                memcpy(g, digits[c - '0'], 5);
            }
            else
            {
                memset(g, 0, 5);
            }

            return;
        }
    }
}

static void OLED_Print(uint8_t x, uint8_t page, const char *s)
{
    while (*s && x <= 122 && page < 8)
    {
        uint8_t g[5];

        OLED_Glyph(*s++, g);

        memcpy(
            &oled_buffer[page * 128 + x],
            g,
            5
        );

        oled_buffer[page * 128 + x + 5] = 0;

        x += 6;
    }
}

static void OLED_Status(const char *line1, const char *line2)
{
    OLED_Clear();

    OLED_Print(0, 1, line1);
    OLED_Print(0, 4, line2);

    OLED_Update();
}

static void OLED_ShowPassword(void)
{
    char masked[PASSWORD_LENGTH + 1];

    for (uint8_t i = 0; i < password_length; i++)
    {
        masked[i] = '*';
    }

    masked[password_length] = '\0';

    OLED_Clear();

    OLED_Print(0, 0, "NOIR SENTINEL");
    OLED_Print(0, 3, "ENTER PASS");
    OLED_Print(0, 5, masked);

    OLED_Update();
}

/* =========================================================
   RC522 SPI DRIVER
   ========================================================= */

static void RFID_Select(void)
{
    HAL_GPIO_WritePin(
        RFID_CS_GPIO_Port,
        RFID_CS_Pin,
        GPIO_PIN_RESET
    );
}

static void RFID_Deselect(void)
{
    HAL_GPIO_WritePin(
        RFID_CS_GPIO_Port,
        RFID_CS_Pin,
        GPIO_PIN_SET
    );
}

static void RFID_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] =
    {
        (uint8_t)((reg << 1) & 0x7E),
        value
    };

    RFID_Select();

    HAL_SPI_Transmit(
        &hspi1,
        data,
        2,
        RFID_TIMEOUT_MS
    );

    RFID_Deselect();
}

static uint8_t RFID_ReadReg(uint8_t reg)
{
    uint8_t address =
        (uint8_t)(((reg << 1) & 0x7E) | 0x80);

    uint8_t value = 0;

    RFID_Select();

    HAL_SPI_Transmit(
        &hspi1,
        &address,
        1,
        RFID_TIMEOUT_MS
    );

    HAL_SPI_Receive(
        &hspi1,
        &value,
        1,
        RFID_TIMEOUT_MS
    );

    RFID_Deselect();

    return value;
}

static void RFID_SetMask(uint8_t reg, uint8_t mask)
{
    RFID_WriteReg(
        reg,
        RFID_ReadReg(reg) | mask
    );
}

static void RFID_ClearMask(uint8_t reg, uint8_t mask)
{
    RFID_WriteReg(
        reg,
        RFID_ReadReg(reg) & (uint8_t)~mask
    );
}

static void RFID_Init(void)
{
    HAL_GPIO_WritePin(
        RFID_RST_GPIO_Port,
        RFID_RST_Pin,
        GPIO_PIN_RESET
    );

    HAL_Delay(5);

    HAL_GPIO_WritePin(
        RFID_RST_GPIO_Port,
        RFID_RST_Pin,
        GPIO_PIN_SET
    );

    HAL_Delay(50);

    RFID_WriteReg(CommandReg, 0x0F);

    HAL_Delay(50);

    RFID_WriteReg(TModeReg, 0x8D);
    RFID_WriteReg(TPrescalerReg, 0x3E);
    RFID_WriteReg(TReloadRegL, 30);
    RFID_WriteReg(TReloadRegH, 0);
    RFID_WriteReg(TxASKReg, 0x40);
    RFID_WriteReg(ModeReg, 0x3D);

    if ((RFID_ReadReg(TxControlReg) & 0x03) != 0x03)
    {
        RFID_SetMask(TxControlReg, 0x03);
    }
}

static HAL_StatusTypeDef RFID_Transceive(
    uint8_t *tx,
    uint8_t tx_len,
    uint8_t *rx,
    uint8_t *rx_len)
{
    uint32_t start;
    uint8_t irq;

    RFID_WriteReg(CommandReg, PCD_Idle);
    RFID_WriteReg(ComIrqReg, 0x7F);
    RFID_WriteReg(FIFOLevelReg, 0x80);

    for (uint8_t i = 0; i < tx_len; i++)
    {
        RFID_WriteReg(FIFODataReg, tx[i]);
    }

    RFID_WriteReg(CommandReg, PCD_Transceive);

    RFID_SetMask(BitFramingReg, 0x80);

    start = HAL_GetTick();

    do
    {
        irq = RFID_ReadReg(ComIrqReg);

        if ((HAL_GetTick() - start) > RFID_TIMEOUT_MS)
        {
            RFID_ClearMask(BitFramingReg, 0x80);
            return HAL_TIMEOUT;
        }

    } while ((irq & 0x30) == 0 && (irq & 0x01) == 0);

    RFID_ClearMask(BitFramingReg, 0x80);

    if (RFID_ReadReg(ErrorReg) & 0x1B)
    {
        return HAL_ERROR;
    }

    uint8_t n = RFID_ReadReg(FIFOLevelReg);

    if (n > *rx_len)
    {
        return HAL_ERROR;
    }

    *rx_len = n;

    for (uint8_t i = 0; i < n; i++)
    {
        rx[i] = RFID_ReadReg(FIFODataReg);
    }

    return HAL_OK;
}

static uint8_t RFID_ReadUID(uint8_t uid[4])
{
    uint8_t tx[2];
    uint8_t rx[10];
    uint8_t n;

    /* Request card */
    tx[0] = PICC_REQA;

    n = sizeof(rx);

    RFID_WriteReg(BitFramingReg, 0x07);

    if (RFID_Transceive(tx, 1, rx, &n) != HAL_OK)
    {
        return 0;
    }

    /* Anticollision */
    tx[0] = PICC_ANTICOLL;
    tx[1] = 0x20;

    n = sizeof(rx);

    RFID_WriteReg(BitFramingReg, 0x00);

    if (RFID_Transceive(tx, 2, rx, &n) != HAL_OK)
    {
        return 0;
    }

    if (n < 5)
    {
        return 0;
    }

    if ((uint8_t)(rx[0] ^ rx[1] ^ rx[2] ^ rx[3]) != rx[4])
    {
        return 0;
    }

    memcpy(uid, rx, 4);

    return 1;
}

/* =========================================================
   TTP229 GPIO KEYPAD
   ========================================================= */

static GPIO_PinState ReadKeyPin(uint8_t i)
{
    switch (i)
    {
        case 0:
            return HAL_GPIO_ReadPin(KEY_1_GPIO_Port, KEY_1_Pin);

        case 1:
            return HAL_GPIO_ReadPin(KEY_2_GPIO_Port, KEY_2_Pin);

        case 2:
            return HAL_GPIO_ReadPin(KEY_3_GPIO_Port, KEY_3_Pin);

        case 3:
            return HAL_GPIO_ReadPin(KEY_4_GPIO_Port, KEY_4_Pin);

        case 4:
            return HAL_GPIO_ReadPin(KEY_5_GPIO_Port, KEY_5_Pin);

        case 5:
            return HAL_GPIO_ReadPin(KEY_6_GPIO_Port, KEY_6_Pin);

        case 6:
            return HAL_GPIO_ReadPin(KEY_7_GPIO_Port, KEY_7_Pin);

        case 7:
            return HAL_GPIO_ReadPin(KEY_8_GPIO_Port, KEY_8_Pin);

        default:
            return GPIO_PIN_RESET;
    }
}

static uint8_t Keypad_GetKey(void)
{
    /*
     * Detect a touch immediately.
     * A key generates one event until it is released.
     */

    for (uint8_t i = 0; i < 8; i++)
    {
        GPIO_PinState state = ReadKeyPin(i);

        if (state == GPIO_PIN_SET)
        {
            if (!key_was_down[i])
            {
                key_was_down[i] = 1;

                return i + 1;
            }
        }
        else
        {
            key_was_down[i] = 0;
        }
    }

    return 0;
}

/* =========================================================
   ACCESS CONTROL
   ========================================================= */

static void Access_Grant(const char *method)
{
    char msg[64];

    snprintf(
        msg,
        sizeof(msg),
        "ACCESS GRANTED: %s\r\n",
        method
    );

    Debug_Print(msg);

    /* Show success immediately */
    OLED_Status("NOIR SENTINEL", "SUCCESS");

    /* Audible success confirmation */
    Buzzer_Success();

    HAL_Delay(1500);

    password_length = 0;

    memset(
        entered_password,
        0,
        sizeof(entered_password)
    );

    OLED_Status("NOIR SENTINEL", "READY");
}

static void Access_Deny(void)
{
    Debug_Print("ACCESS DENIED\r\n");

    /* Show DENIED before lockout message */
    OLED_Status("NOIR SENTINEL", "DENIED");

    Buzzer_Failed();

    HAL_Delay(1500);

    password_length = 0;

    memset(
        entered_password,
        0,
        sizeof(entered_password)
    );

    locked_out = 1;
    lockout_start = HAL_GetTick();

    OLED_Status("NOIR SENTINEL", "WAIT 10 SEC");
}

static void ProcessKey(uint8_t key)
{
    char digit;

    /*
     * PIN = 1823
     *
     * KEY2 -> 1
     * KEY8 -> 8
     * KEY3 -> 2
     * KEY4 -> 3
     */

    switch (key)
    {
        case 2:
            digit = '1';
            break;

        case 8:
            digit = '8';
            break;

        case 3:
            digit = '2';
            break;

        case 4:
            digit = '3';
            break;

        default:
            /* Ignore keys not assigned to the PIN */
            return;
    }

    /* Update entered PIN */
    if (password_length < PASSWORD_LENGTH)
    {
        entered_password[password_length] = digit;
        password_length++;

        entered_password[password_length] = '\0';
    }

    /* Refresh OLED after each accepted key */
    OLED_ShowPassword();

    /* Short feedback for a valid PIN key */
    Buzzer_Short();

    /* Validate immediately after four digits */
    if (password_length == PASSWORD_LENGTH)
    {
        if (strcmp(entered_password, password) == 0)
        {
            Access_Grant("PASSWORD");
        }
        else
        {
            Access_Deny();
        }
    }
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_SPI1_Init();
    MX_I2C1_Init();

    Buzzer_Set(0);

    Debug_Print("\r\nNOIR SENTINEL STARTING\r\n");

    /* Initialize OLED */
    OLED_Init();

    OLED_Status("NOIR SENTINEL", "STARTING");

    /* Initialize RFID reader */
    RFID_Init();

    char msg[64];

    snprintf(
        msg,
        sizeof(msg),
        "RC522 VersionReg = 0x%02X\r\n",
        RFID_ReadReg(VersionReg)
    );

    Debug_Print(msg);

    OLED_Status("NOIR SENTINEL", "READY");

    Debug_Print("Demo PIN: KEY2 KEY8 KEY3 KEY4 = 1823\r\n");

    while (1)
    {
        /* -----------------------------------------
           Temporary lockout
           ----------------------------------------- */

        if (locked_out)
        {
            if ((HAL_GetTick() - lockout_start) >= LOCKOUT_MS)
            {
                locked_out = 0;

                Buzzer_Beep(100);

                OLED_Status("NOIR SENTINEL", "READY");

                Debug_Print("LOCKOUT FINISHED\r\n");
            }

            HAL_Delay(5);

            continue;
        }

        /* -----------------------------------------
           RFID polling
           ----------------------------------------- */

        uint8_t uid[4];

        if (RFID_ReadUID(uid))
        {
            snprintf(
                msg,
                sizeof(msg),
                "RFID UID: %02X %02X %02X %02X\r\n",
                uid[0],
                uid[1],
                uid[2],
                uid[3]
            );

            Debug_Print(msg);

            /*
             * Immediate feedback when a readable card
             * is detected.
             */
            Buzzer_Short();

#if RFID_ACCEPT_ANY_CARD
            Access_Grant("RFID");
#else
            Access_Deny();
#endif

            /*
             * Avoid processing the same card repeatedly.
             * This is a simple polling-based cooldown.
             */
            HAL_Delay(300);

            continue;
        }

        /* -----------------------------------------
           Keypad polling
           ----------------------------------------- */

        uint8_t key = Keypad_GetKey();

        if (key != 0)
        {
            snprintf(
                msg,
                sizeof(msg),
                "KEY_%u touched\r\n",
                key
            );

            Debug_Print(msg);

            ProcessKey(key);
        }

        /*
         * Short polling interval to improve response.
         */
        HAL_Delay(2);
    }
}

/* =========================================================
   CUBEMX INITIALIZATION
   Keep stm32f0xx_hal_msp.c unchanged.
   ========================================================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(
        RFID_CS_GPIO_Port,
        RFID_CS_Pin,
        GPIO_PIN_SET
    );

    HAL_GPIO_WritePin(
        RFID_RST_GPIO_Port,
        RFID_RST_Pin,
        GPIO_PIN_SET
    );

    HAL_GPIO_WritePin(
        BUZZER_SIGNAL_GPIO_Port,
        BUZZER_SIGNAL_Pin,
        GPIO_PIN_RESET
    );

    /* B1: PC13 */
    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    /* Keypad inputs on GPIOA */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    GPIO_InitStruct.Pin =
        KEY_1_Pin |
        KEY_2_Pin |
        KEY_3_Pin;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Keypad input on GPIOB */
    GPIO_InitStruct.Pin = KEY_4_Pin;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Keypad inputs on GPIOC */
    GPIO_InitStruct.Pin =
        KEY_5_Pin |
        KEY_6_Pin |
        KEY_7_Pin |
        KEY_8_Pin;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* RFID CS: PB6 */
    GPIO_InitStruct.Pin = RFID_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(
        RFID_CS_GPIO_Port,
        &GPIO_InitStruct
    );

    /* RFID RST: PC7 */
    GPIO_InitStruct.Pin = RFID_RST_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(
        RFID_RST_GPIO_Port,
        &GPIO_InitStruct
    );

    /* Buzzer: PA9 */
    GPIO_InitStruct.Pin = BUZZER_SIGNAL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(
        BUZZER_SIGNAL_GPIO_Port,
        &GPIO_InitStruct
    );

    HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;

    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;

    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;

    hi2c1.Init.Timing = 0x2000090E;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_I2CEx_ConfigAnalogFilter(
            &hi2c1,
            I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_I2CEx_ConfigDigitalFilter(
            &hi2c1,
            0) != HAL_OK)
    {
        Error_Handler();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue =
        RCC_HSICALIBRATION_DEFAULT;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL12;
    RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }

    PeriphClkInit.PeriphClockSelection =
        RCC_PERIPHCLK_USART2 |
        RCC_PERIPHCLK_I2C1;

    PeriphClkInit.Usart2ClockSelection =
        RCC_USART2CLKSOURCE_PCLK1;

    PeriphClkInit.I2c1ClockSelection =
        RCC_I2C1CLKSOURCE_HSI;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT

void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}

#endif
