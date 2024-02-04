#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

#define SCREEN_WIDTH    960
#define SCREEN_HEIGHT   544

#define EVENT_BUF_SIZE 64

static SDL_Event events[EVENT_BUF_SIZE];
static int eventWrite;
static int colors[7] = {0xFF,0xFF00,0xFF0000,0xFFFF00,0x00FFFF,0xFF00FF,0xFFFFFF};

/* This is indexed by SDL_GameControllerButton. */
static const struct { int x; int y; } button_positions[] = {
    {842, 241},  /* SDL_CONTROLLER_BUTTON_A */
    {884, 201},  /* SDL_CONTROLLER_BUTTON_B */
    {800, 201},  /* SDL_CONTROLLER_BUTTON_X */
    {842, 162},  /* SDL_CONTROLLER_BUTTON_Y */

    {802, 398},  /* SDL_CONTROLLER_BUTTON_BACK */
    {80, 398},  /* SDL_CONTROLLER_BUTTON_GUIDE */
    {854, 398},  /* SDL_CONTROLLER_BUTTON_START */

    {92,  316},  /* SDL_CONTROLLER_BUTTON_LEFTSTICK */
    {816, 316},  /* SDL_CONTROLLER_BUTTON_RIGHTSTICK */

    {92,  74},   /* SDL_CONTROLLER_BUTTON_LEFTSHOULDER */
    {814, 74},   /* SDL_CONTROLLER_BUTTON_RIGHTSHOULDER */

    {66, 166},  /* SDL_CONTROLLER_BUTTON_DPAD_UP */
    {66, 238},  /* SDL_CONTROLLER_BUTTON_DPAD_DOWN */
    {30, 202},  /* SDL_CONTROLLER_BUTTON_DPAD_LEFT */
    {100, 202},  /* SDL_CONTROLLER_BUTTON_DPAD_RIGHT */

    {232, 174},  /* SDL_CONTROLLER_BUTTON_MISC1 */
    {132, 135},  /* SDL_CONTROLLER_BUTTON_PADDLE1 */
    {330, 135},  /* SDL_CONTROLLER_BUTTON_PADDLE2 */
    {132, 175},  /* SDL_CONTROLLER_BUTTON_PADDLE3 */
    {330, 175},  /* SDL_CONTROLLER_BUTTON_PADDLE4 */
};

/* This is indexed by SDL_GameControllerAxis. */
static const struct { int x; int y; double angle; } axis_positions[] = {
    {94,  318, 270.0},  /* LEFTX */
    {94,  318,   0.0},  /* LEFTY */

    {818, 318, 270.0},  /* RIGHTX */
    {818, 318,   0.0},  /* RIGHTY */

    {92,  20,   0.0},  /* TRIGGERLEFT */
    {814, 20,   0.0},  /* TRIGGERRIGHT */
};

SDL_Window *window = NULL;
SDL_Renderer *screen = NULL;
SDL_bool retval = SDL_FALSE;
SDL_bool done = SDL_FALSE;
SDL_bool set_LED = SDL_FALSE;
SDL_Texture *background_front, *button, *axis, *touch_front, *touch_back;
SDL_GameController *gamecontroller;
SDL_GameController **gamecontrollers;
int num_controllers = 0;

static int FindController(SDL_JoystickID controller_id)
{
    int i;

    for (i = 0; i < num_controllers; ++i) {
        if (controller_id == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamecontrollers[i]))) {
            return i;
        }
    }
    return -1;
}

static void AddController(int device_index, SDL_bool verbose)
{
    SDL_JoystickID controller_id = SDL_JoystickGetDeviceInstanceID(device_index);
    SDL_GameController *controller;
    SDL_GameController **controllers;

    controller_id = SDL_JoystickGetDeviceInstanceID(device_index);
    if (controller_id < 0) {
        SDL_Log("Couldn't get controller ID: %s\n", SDL_GetError());
        return;
    }

    if (FindController(controller_id) >= 0) {
        /* We already have this controller */
        return;
    }

    controller = SDL_GameControllerOpen(device_index);
    if (!controller) {
        SDL_Log("Couldn't open controller: %s\n", SDL_GetError());
        return;
    }

    controllers = (SDL_GameController **)SDL_realloc(gamecontrollers, (num_controllers + 1) * sizeof(*controllers));
    if (!controllers) {
        SDL_GameControllerClose(controller);
        return;
    }

    controllers[num_controllers++] = controller;
    gamecontrollers = controllers;
    gamecontroller = controller;

    if (verbose) {
        const char *name = SDL_GameControllerName(gamecontroller);
        //SDL_Log("Opened game controller %s\n", name);
    }
}

static void SetController(SDL_JoystickID controller)
{
    int i = FindController(controller);

    if (i < 0) {
        return;
    }

    if (gamecontroller != gamecontrollers[i]) {
        gamecontroller = gamecontrollers[i];
    }
}

static void DelController(SDL_JoystickID controller)
{
    int i = FindController(controller);

    if (i < 0) {
        return;
    }

    SDL_GameControllerClose(gamecontrollers[i]);

    --num_controllers;
    if (i < num_controllers) {
        SDL_memcpy(&gamecontrollers[i], &gamecontrollers[i+1], (num_controllers - i) * sizeof(*gamecontrollers));
    }

    if (num_controllers > 0) {
        gamecontroller = gamecontrollers[0];
    } else {
        gamecontroller = NULL;
    }
}

static SDL_Texture *
LoadTexture(SDL_Renderer *renderer, const char *file, SDL_bool transparent)
{
    SDL_Surface *temp = NULL;
    SDL_Texture *texture = NULL;

    temp = SDL_LoadBMP(file);
    if (temp == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", file, SDL_GetError());
    } else {
        /* Set transparent pixel as the pixel at (0,0) */
        if (transparent) {
            if (temp->format->BytesPerPixel == 1) {
                SDL_SetColorKey(temp, SDL_TRUE, *(Uint8 *)temp->pixels);
            }
        }

        texture = SDL_CreateTextureFromSurface(renderer, temp);
        if (!texture) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create texture: %s\n", SDL_GetError());
        }
    }
    if (temp) {
        SDL_FreeSurface(temp);
    }
    return texture;
}

static Uint16 ConvertAxisToRumble(Sint16 axis)
{
    /* Only start rumbling if the axis is past the halfway point */
    const Sint16 half_axis = (Sint16)SDL_ceil(SDL_JOYSTICK_AXIS_MAX / 2.0f);
    if (axis > half_axis) {
        return (Uint16)(axis - half_axis) * 4;
    } else {
        return 0;
    }
}

static float gx = 0.0f;
static float gy = 0.0f;
static float gz = 0.0f;
static float ax = 0.0f;
static float ay = 0.0f;
static float az = 0.0f;

void
loop(void *arg)
{
    SDL_Event event;
    int i;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_CONTROLLERDEVICEADDED:
            //SDL_Log("Game controller device %d added.\n", (int) SDL_JoystickGetDeviceInstanceID(event.cdevice.which));
            AddController(event.cdevice.which, SDL_TRUE);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            //SDL_Log("Game controller device %d removed.\n", (int) event.cdevice.which);
            DelController(event.cdevice.which);
            break;

        case SDL_CONTROLLERAXISMOTION:
            if (event.caxis.value <= (-SDL_JOYSTICK_AXIS_MAX / 2) || event.caxis.value >= (SDL_JOYSTICK_AXIS_MAX / 2)) {
                SetController(event.caxis.which);
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                SetController(event.cbutton.which);
            }
            break;

        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            events[eventWrite & (EVENT_BUF_SIZE-1)] = event;
            eventWrite++;
            break;
        case SDL_SENSORUPDATE:
            {
                SDL_Sensor *sensor = SDL_SensorFromInstanceID(event.sensor.which);
                switch (SDL_SensorGetType(sensor)) {
                    case SDL_SENSOR_ACCEL:
                        ax = event.sensor.data[0];
                        ay = event.sensor.data[1];
                        az = event.sensor.data[2];
                        break;
                    case SDL_SENSOR_GYRO:
                        gx = event.sensor.data[0];
                        gy = event.sensor.data[1];
                        gz = event.sensor.data[2];
                        break;
                    default:
                        break;
                }
            }
            break;

        case SDL_KEYDOWN:
            if (event.key.keysym.sym != SDLK_ESCAPE) {
                break;
            }
            /* Fall through to signal quit */
        case SDL_QUIT:
            done = SDL_TRUE;
            break;
        default:
            break;
        }
    }

    /* blank screen, set up for drawing this frame. */
    SDL_SetRenderDrawColor(screen, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(screen);
    SDL_RenderCopy(screen, background_front, NULL, NULL);

    for (int i = eventWrite; i < eventWrite + EVENT_BUF_SIZE; ++i) {
        const SDL_Event *event = &events[i & (EVENT_BUF_SIZE - 1)];
        int x, y;

        if ( (event->type == SDL_FINGERMOTION) ||
             (event->type == SDL_FINGERDOWN) ||
             (event->type == SDL_FINGERUP) ) {
            x = 190 + event->tfinger.x * 580;
            y = 116 + event->tfinger.y * 325;

            const SDL_Rect dst = { x-5, y-5, 10, 10 };

            if (event->tfinger.touchId > 0)
                SDL_RenderCopy(screen, touch_back, NULL, &dst);
            else
                SDL_RenderCopy(screen, touch_front, NULL, &dst);
        }
    }


    const SDL_Rect recta = { 320, 10, ax*10, 10 };
    const SDL_Rect rectb = { 320, 20, ay*10, 10 };
    const SDL_Rect rectc = { 320, 30, az*10, 10 };
    SDL_SetRenderDrawColor(screen, 0xFF, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(screen, &recta);
    SDL_RenderFillRect(screen, &rectb);
    SDL_RenderFillRect(screen, &rectc);


    const SDL_Rect rectga = { 640, 10, gx*10, 10 };
    const SDL_Rect rectgb = { 640, 20, gy*10, 10 };
    const SDL_Rect rectgc = { 640, 30, gz*10, 10 };
    SDL_SetRenderDrawColor(screen, 0x00, 0xFF, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(screen, &rectga);
    SDL_RenderFillRect(screen, &rectgb);
    SDL_RenderFillRect(screen, &rectgc);

    if (gamecontroller) {
        /* Update visual controller state */
        for (i = 0; i < SDL_CONTROLLER_BUTTON_TOUCHPAD; ++i) {
            if (SDL_GameControllerGetButton(gamecontroller, (SDL_GameControllerButton)i) == SDL_PRESSED) {
                const SDL_Rect dst = { button_positions[i].x, button_positions[i].y, 50, 50 };
                SDL_RenderCopyEx(screen, button, NULL, &dst, 0, NULL, SDL_FLIP_NONE);
            }
        }

        for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
            const Sint16 deadzone = 8000;
            const Sint16 value = SDL_GameControllerGetAxis(gamecontroller, (SDL_GameControllerAxis)(i));
            if (value < -deadzone) {
                const SDL_Rect dst = { axis_positions[i].x, axis_positions[i].y, 50, 50 };
                const double angle = axis_positions[i].angle;
                SDL_RenderCopyEx(screen, axis, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
            } else if (value > deadzone) {
                const SDL_Rect dst = { axis_positions[i].x, axis_positions[i].y, 50, 50 };
                const double angle = axis_positions[i].angle + 180.0;
                SDL_RenderCopyEx(screen, axis, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
            }
        }

        /* Update LED based on left thumbstick position */
        {
            Sint16 x = SDL_GameControllerGetAxis(gamecontroller, SDL_CONTROLLER_AXIS_LEFTX);
            Sint16 y = SDL_GameControllerGetAxis(gamecontroller, SDL_CONTROLLER_AXIS_LEFTY);

            if (!set_LED) {
                set_LED = (x < -8000 || x > 8000 || y > 8000);
            }
            if (set_LED) {
                Uint8 r, g, b;

                if (x < 0) {
                    r = (Uint8)(((int)(~x) * 255) / 32767);
                    b = 0;
                } else {
                    r = 0;
                    b = (Uint8)(((int)(x) * 255) / 32767);
                }
                if (y > 0) {
                    g = (Uint8)(((int)(y) * 255) / 32767);
                } else {
                    g = 0;
                }

                SDL_GameControllerSetLED(gamecontroller, r, g, b);
            }
        }

        /* Update rumble based on trigger state */
        {
            Sint16 left = SDL_GameControllerGetAxis(gamecontroller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            Sint16 right = SDL_GameControllerGetAxis(gamecontroller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            Uint16 low_frequency_rumble = ConvertAxisToRumble(left);
            Uint16 high_frequency_rumble = ConvertAxisToRumble(right);
            SDL_GameControllerRumble(gamecontroller, low_frequency_rumble, high_frequency_rumble, 250);
        }

        /* Update trigger rumble based on thumbstick state */
        {
            Sint16 left = SDL_GameControllerGetAxis(gamecontroller, SDL_CONTROLLER_AXIS_LEFTY);
            Sint16 right = SDL_GameControllerGetAxis(gamecontroller, SDL_CONTROLLER_AXIS_RIGHTY);
            Uint16 left_rumble = ConvertAxisToRumble(~left);
            Uint16 right_rumble = ConvertAxisToRumble(~right);

            SDL_GameControllerRumbleTriggers(gamecontroller, left_rumble, right_rumble, 250);
        }
    }

    SDL_RenderPresent(screen);

#ifdef __EMSCRIPTEN__
    if (done) {
        emscripten_cancel_main_loop();
    }
#endif
}

static const char *GetSensorTypeString(SDL_SensorType type)
{
    static char unknown_type[64];

    switch (type)
    {
    case SDL_SENSOR_INVALID:
        return "SDL_SENSOR_INVALID";
    case SDL_SENSOR_UNKNOWN:
        return "SDL_SENSOR_UNKNOWN";
    case SDL_SENSOR_ACCEL:
        return "SDL_SENSOR_ACCEL";
    case SDL_SENSOR_GYRO:
        return "SDL_SENSOR_GYRO";
    default:
        SDL_snprintf(unknown_type, sizeof(unknown_type), "UNKNOWN (%d)", type);
        return unknown_type;
    }
}


int
main(int argc, char *argv[])
{
    int i;
    int controller_count = 0;
    int controller_index = 0;
    char guid[64];
    int num_sensors, num_opened;

    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");

    /* Enable standard application logging */
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_ERROR);

    /* Initialize SDL (Note: video is required to start event loop) */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    num_sensors = SDL_NumSensors();
    num_opened = 0;

    for (i = 0; i < num_sensors; ++i) {

        if (SDL_SensorGetDeviceType(i) != SDL_SENSOR_UNKNOWN) {
            SDL_Sensor *sensor = SDL_SensorOpen(i);
            if (sensor == NULL) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open sensor %" SDL_PRIs32 ": %s\n", SDL_SensorGetDeviceInstanceID(i), SDL_GetError());
            } else {
                ++num_opened;
            }
        }
    }

    /* Print information about the controller */
    for (i = 0; i < SDL_NumJoysticks(); ++i) {
        const char *name;
        const char *description;

        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                  guid, sizeof (guid));

        if (SDL_IsGameController(i)) {
            controller_count++;
            name = SDL_GameControllerNameForIndex(i);
            switch (SDL_GameControllerTypeForIndex(i)) {
            case SDL_CONTROLLER_TYPE_XBOX360:
                description = "XBox 360 Controller";
                break;
            case SDL_CONTROLLER_TYPE_XBOXONE:
                description = "XBox One Controller";
                break;
            case SDL_CONTROLLER_TYPE_PS3:
                description = "PS3 Controller";
                break;
            case SDL_CONTROLLER_TYPE_PS4:
                description = "PS4 Controller";
                break;
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
                description = "Nintendo Switch Pro Controller";
                break;
            case SDL_CONTROLLER_TYPE_VIRTUAL:
                description = "Virtual Game Controller";
                break;
            default:
                description = "Game Controller";
                break;
            }
            AddController(i, SDL_TRUE);
        } else {
            name = SDL_JoystickNameForIndex(i);
            description = "Joystick";
        }
/*
        SDL_Log("%s %d: %s (guid %s, VID 0x%.4x, PID 0x%.4x, player index = %d)\n",
            description, i, name ? name : "Unknown", guid,
            SDL_JoystickGetDeviceVendor(i), SDL_JoystickGetDeviceProduct(i), SDL_JoystickGetDevicePlayerIndex(i));
*/
    }
//    SDL_Log("There are %d game controller(s) attached (%d joystick(s))\n", controller_count, SDL_NumJoysticks());

    /* Create a window to display controller state */
    window = SDL_CreateWindow("Game Controller Test", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 960,
                              544, 0);
    if (window == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window: %s\n", SDL_GetError());
        return 2;
    }

    screen = SDL_CreateRenderer(window, -1, 0);
    if (screen == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        return 2;
    }

    SDL_SetRenderDrawColor(screen, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(screen);
    SDL_RenderPresent(screen);

    /* scale for platforms that don't give you the window size you asked for. */
    SDL_RenderSetLogicalSize(screen, SCREEN_WIDTH, SCREEN_HEIGHT);

    background_front = LoadTexture(screen, "data/controllermap.bmp", SDL_FALSE);
    button = LoadTexture(screen, "data/button.bmp", SDL_TRUE);
    axis = LoadTexture(screen, "data/axis.bmp", SDL_TRUE);


    touch_front = LoadTexture(screen, "data/button.bmp", SDL_TRUE);
    touch_back = LoadTexture(screen, "data/button.bmp", SDL_TRUE);

    if (!background_front || !button || !axis || !touch_front || !touch_back) {
        SDL_DestroyRenderer(screen);
        SDL_DestroyWindow(window);
        return 2;
    }
    SDL_SetTextureColorMod(button, 10, 255, 21);
    SDL_SetTextureAlphaMod(button, 100);
    SDL_SetTextureColorMod(axis, 10, 255, 21);

    SDL_SetTextureColorMod(touch_front, 10, 255, 21);
    SDL_SetTextureAlphaMod(touch_front, 100);

    SDL_SetTextureColorMod(touch_back, 255, 10, 21);
    SDL_SetTextureAlphaMod(touch_back, 100);

    if (controller_index < num_controllers) {
        gamecontroller = gamecontrollers[controller_index];
    } else {
        gamecontroller = NULL;
    }

    while (!done) {
        loop(NULL);
    }

    SDL_DestroyRenderer(screen);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);

    return 0;
}
