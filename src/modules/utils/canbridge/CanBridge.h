#ifndef CANBRIDGE_H
#define CANBRIDGE_H

#include "libs/Module.h"
#include <string>

class Gcode;

class CanBridge : public Module {
    public:
        CanBridge();
        virtual ~CanBridge() {}

        void on_module_loaded();
        void on_gcode_received(void* argument);
        void on_second_tick(void* argument);

    private:
        bool     can_enable;
        uint32_t can_baudrate;
        static volatile uint32_t last_x_msg_ms;
        static volatile uint32_t last_z_msg_ms;
        bool x_is_failed;
        bool z_is_failed;
        bool can_coordinate;
};

#endif // CANBRIDGE_H
