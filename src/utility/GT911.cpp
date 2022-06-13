#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>
#include "GT911.h"

static const uint8_t _kGT911FW540960G2T1602729168[] = {
    0x43, 0x1C, 0x02, 0xC0, 0x03, 0x02, 0x05, 0x00, 0x01, 0x18, 0x28, 0x0F, 0x50, 0x32,
    0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1A, 0x1E, 0x14, 0x87,
    0x29, 0x0A, 0x21, 0x23, 0xB2, 0x04, 0x00, 0x00, 0x00, 0x1A, 0x02, 0x1C, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x64, 0x32, 0x00, 0x00, 0x00, 0x19, 0x41, 0x94, 0xC5, 0x02,
    0x07, 0x00, 0x00, 0x04, 0x98, 0x1B, 0x00, 0x7F, 0x21, 0x00, 0x6A, 0x28, 0x00, 0x58,
    0x31, 0x00, 0x4A, 0x3B, 0x00, 0x4A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0x18, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x28, 0x26, 0x24, 0x22, 0x21, 0x20, 0x1F, 0x1E, 0x1D, 0x00, 0x02, 0x04,
    0x06, 0x08, 0x0A, 0x0C, 0x0F, 0x10, 0x12, 0x13, 0x14, 0x16, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xCE, 0x01};

GT911::GT911() {}

const uint32_t TOUCH_TASK_STACK_DEPTH = 1024 * 8;
const UBaseType_t TOUCH_TASK_PRIORITY = 10;
static TaskHandle_t xTouchTaskHandle = NULL;
FingerDownCallbackFunction callbackFingerDown;
FingerUpCallbackFunction callbackFingerUp;

volatile uint8_t gt911_irq_trigger = 0;
void ICACHE_RAM_ATTR ___GT911IRQ___()
{
    noInterrupts();
    gt911_irq_trigger = 1;
    interrupts();
}

void ICACHE_RAM_ATTR vTouchISR()
{
    gt911_irq_trigger = 1;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveIndexedFromISR(xTouchTaskHandle, 0, &xHigherPriorityTaskWoken);

    // Force context switch, if needed
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void vTouchHandlerTask(void *pvParameters)
{
    GT911 *tp = static_cast<GT911 *>(pvParameters);

    bool wasDown = false;

    for (;;)
    {

        // Block until new notification from the interrupt
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);

        if (!tp->_available())
            continue;

        // Call finger down callback on every loop,
        // but call finger up callback only once per touch.
        if (tp->_update())
        {
            callbackFingerDown(tp->_finger);
            wasDown = true;
        }
        else if (wasDown)
        {
            callbackFingerUp();
            wasDown = false;
        }
    }

    // Should never get here, but if we do, make sure to exit cleanly
    vTaskDelete(NULL);
}

void stopTouchHandlerTask()
{
    if (xTouchTaskHandle != NULL)
    {
        vTaskDelete(xTouchTaskHandle);
        xTouchTaskHandle = NULL;
    }
}

void GT911::onTouch(const FingerDownCallbackFunction &cbFingerDown, const FingerUpCallbackFunction &cbFingerUp)
{
    if (callbackFingerDown)
        return;

    callbackFingerDown = cbFingerDown;
    callbackFingerUp = cbFingerUp;

    BaseType_t taskCreateResult = xTaskCreate(
        vTouchHandlerTask,
        "TOUCH_TASK",
        TOUCH_TASK_STACK_DEPTH,
        (void *)this,
        TOUCH_TASK_PRIORITY,
        &xTouchTaskHandle);

    attachInterrupt(_pin_int, vTouchISR, FALLING);
}

esp_err_t GT911::begin(uint8_t pin_sda, uint8_t pin_scl, uint8_t pin_int)
{
    log_d("GT911: Initialization");
    _pin_int = pin_int;
    pinMode(pin_int, INPUT); // Startup sequence PIN part

    Wire.begin((int)pin_sda, (int)pin_scl, (uint32_t)100000U); // Note: SHT3x sensor built into M5Paper only seems to work with default 100kHz
    delay(100);

    Wire.beginTransmission(0x14);
    if (Wire.endTransmission())
    {
        Wire.beginTransmission(0x5D);
        if (Wire.endTransmission())
        {
            log_e("Touch screen IIC connection error");
            return ESP_FAIL;
        }
        _iic_addr = 0x5D;
    }

    // if(read(0x8047) != _kGT911FW540960G2T1602729168[0])
    // {
    //     log_d("GT911: Update firmware");
    //     write(0x8040, 0x02);
    //     delay(100);
    //     write(0x8047, _kGT911FW540960G2T1602729168, 186);
    //     delay(50);
    //     write(0x8040, 0x00);
    //     delay(100);
    // }

    // attachInterrupt(pin_int, ___GT911IRQ___, FALLING);

    return ESP_OK;
}

void GT911::write(uint16_t addr, uint8_t data)
{
    Wire.beginTransmission(_iic_addr);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)addr);
    Wire.write(data);
    Wire.endTransmission(true);
}

void GT911::write(uint16_t addr, const uint8_t *data, uint16_t len)
{
    Wire.beginTransmission(_iic_addr);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)addr);
    Wire.write(data, len);
    Wire.endTransmission(true);
}

uint8_t GT911::read(uint16_t addr)
{
    Wire.flush();
    Wire.beginTransmission(_iic_addr);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)addr);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)_iic_addr, (uint8_t)1);
    return Wire.read();
}

void GT911::read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    Wire.flush();
    Wire.beginTransmission(_iic_addr);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)addr);
    Wire.endTransmission(false);
    Wire.requestFrom((int)_iic_addr, (int)len);
    Wire.readBytes(buf, len);
}

uint8_t calcChecksum(const uint8_t *buf, uint8_t len)
{
    uint8_t ccsum = 0;
    for (int i = 0; i < len; i++)
    {
        ccsum += buf[i];
    }
    ccsum = (~ccsum) + 1;
    return ccsum;
}

bool GT911::_available()
{
    if (gt911_irq_trigger == 1)
    {
        gt911_irq_trigger = 0;
        return true;
    }
    return false;
}

void GT911::flush(void)
{
    write(0x814E, 0x00);
    gt911_irq_trigger = 0;
}

bool GT911::_update()
{
    uint8_t r814e = read(0x814E);
    uint8_t num = r814e & 0x0F;
    bool fingerDown = false;

    if (r814e & 0x80)
    {
        if (num != 0)
        {
            fingerDown = true;
            uint8_t data[num * 8];
            read(0x8150, data, num * 8);
            int j = 0;
            uint8_t *buf = data + j * 8;

            if (_rotate == ROTATE_0)
            {
                _finger.x = (buf[3] << 8) | buf[2];
                _finger.y = 540 - ((buf[1] << 8) | buf[0]);
            }
            else if (_rotate == ROTATE_180)
            {
                _finger.x = 960 - ((buf[3] << 8) | buf[2]);
                _finger.y = (buf[1] << 8) | buf[0];
            }
            else if (_rotate == ROTATE_270)
            {
                _finger.x = 540 - ((buf[1] << 8) | buf[0]);
                _finger.y = 960 - ((buf[3] << 8) | buf[2]);
            }
            else
            {
                _finger.x = (buf[1] << 8) | buf[0];
                _finger.y = (buf[3] << 8) | buf[2];
            }

            _finger.size = (buf[5] << 8) | buf[4];
            _finger.id = buf[7];
        }
        write(0x814E, 0x00);
    }

    return fingerDown;
}

void GT911::SetRotation(uint16_t rotate)
{
    if (rotate < 4)
    {
        this->_rotate = rotate;
    }
    else if (rotate < 90)
    {
        this->_rotate = ROTATE_0;
    }
    else if (rotate < 180)
    {
        this->_rotate = ROTATE_90;
    }
    else if (rotate < 270)
    {
        this->_rotate = ROTATE_180;
    }
    else
    {
        this->_rotate = ROTATE_270;
    }
}
