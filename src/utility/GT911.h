#ifndef GT911_H
#define GT911_H

#include <stdint.h>

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t id;
    uint16_t size;
}tp_finger_t;

typedef std::function<void(tp_finger_t)> FingerDownCallbackFunction;
typedef std::function<void()> FingerUpCallbackFunction;

class GT911
{
public:
    static const uint8_t ROTATE_0 = 0;
    static const uint8_t ROTATE_90 = 1;
    static const uint8_t ROTATE_180 = 2;
    static const uint8_t ROTATE_270 = 3;

public:
    GT911();
    esp_err_t begin(uint8_t pin_sda, uint8_t pin_scl, uint8_t pin_int);

    void SetRotation(uint16_t rotate);
    void flush(void);
    void onTouch(const FingerDownCallbackFunction &callbackFingerDown, const FingerUpCallbackFunction &callbackFingerUp);

    bool _available();
    bool _update();

    tp_finger_t _finger;

private:

    void write(uint16_t addr, uint8_t data);
    void write(uint16_t addr, const uint8_t *data, uint16_t len);
    uint8_t read(uint16_t addr);
    void read(uint16_t addr, uint8_t *buf, uint16_t len);
    uint8_t calcChecksum(const uint8_t *buf, uint8_t len);


private:
    uint8_t _rotate = ROTATE_0;
    uint8_t _iic_addr = 0x14;
    uint8_t _pin_int = 0;
};

#endif
