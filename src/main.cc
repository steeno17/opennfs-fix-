#include <GL/glew.h>

#include <opennfs/scene.h>
#include <opennfs/scene_handler.h>
#include <opennfs/sdl.h>
#include <opennfs/opengl.h>
#include <opennfs/window.h>
#include <opennfs/skydome.h>
#include <opennfs/cloud.h>
#include <opennfs/camera.h>
#include <opennfs/world_time.h>
#include <opennfs/config_util.h>
#include <opennfs/level.h>
#include <opennfs/car.h>

#define KIA_RIO_YELLOW 0
#define KIA_RIO_BLUE 1
#define KIA_RIO_RED 2
#define CLOUD_LAYER 3

static game::WorldTime world_time(2014, 8, 1, 14, 0, 0);
static visualizer::Camera camera(0.0f, 20.0f, -700.0f, 90.0f, 0.0f, 1.5f);

bool anisotropic = false;

// ---------------------------------------------------------------- driving --
// The Corvette model is ~600 units long while the Alps road is only ~4 units
// wide (the terrain is loaded at scale 0.05), so the car is scaled right down.
static const float CAR_SCALE = 0.0045f;
// Rotation applied to the car model so its nose points along the heading.
// The model's long axis is x; if the car drives backwards, set this to 180.
static const float CAR_MODEL_YAW_OFFSET = 0.0f;
// Start of the road (world coordinates) and the direction it points in.
static const float CAR_START_X = 0.0f;
static const float CAR_START_Z = -2670.0f;
static const float CAR_START_HEADING = 90.0f;

static const float CHASE_DISTANCE = 7.0f; // behind the car
static const float CHASE_HEIGHT = 2.6f; // above the car
static const float CHASE_LOOK_HEIGHT = 0.8f;
static const float CHASE_FOLLOW = 7.0f; // higher = tighter camera
static const float CHASE_CLEARANCE = 1.0f; // minimum height above the ground

static visualizer::Model* terrain = NULL;
static visualizer::Model* playerCar = NULL;
static game::Car car;
static bool chase_mode = true;
static bool chase_init = false;
static visualizer::types::XYZ chase_pos = { 0.0f, 0.0f, 0.0f };

static void place_player_model() {
    playerCar->positionInstance(0, car.x, car.y, car.z);
    playerCar->rotateInstance(0, 0.0f, 1.0f, 0.0f,
        CAR_MODEL_YAW_OFFSET - car.heading);
}

// resets the car back to the original position
static void reset_car() {
    float y = 0.0f;
    terrain->ground_height(CAR_START_X, CAR_START_Z, 1000.0f, &y);
    car.place(CAR_START_X, y, CAR_START_Z, CAR_START_HEADING);
    chase_init = false;
    place_player_model();
}

static void drive_car(visualizer::Scene* s, float dt) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    game::CarControls in;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        in.throttle = 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        in.brake = 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        in.steer += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        in.steer -= 1.0f;

    car.update(dt, in);

    // stick to the terrain; stop at the edge of the map
    float ground_y;
    if (terrain->ground_height(car.x, car.z, car.y, &ground_y)) {
        car.y = ground_y;
    } else {
        car.revert_move();
    }
    place_player_model();

    // chase camera: behind and above the car, smoothed, never underground
    visualizer::types::XYZ want;
    want.x = car.x - car.forward_x() * CHASE_DISTANCE;
    want.y = car.y + CHASE_HEIGHT;
    want.z = car.z - car.forward_z() * CHASE_DISTANCE;
    float cam_ground;
    if (terrain->ground_height(want.x, want.z, want.y, &cam_ground)
        && want.y < cam_ground + CHASE_CLEARANCE) {
        want.y = cam_ground + CHASE_CLEARANCE;
    }
    if (!chase_init) {
        chase_pos = want;
        chase_init = true;
    } else {
        float k = 1.0f - expf(-CHASE_FOLLOW * dt);
        chase_pos.x += (want.x - chase_pos.x) * k;
        chase_pos.y += (want.y - chase_pos.y) * k;
        chase_pos.z += (want.z - chase_pos.z) * k;
    }
    s->eye(chase_pos);
    s->forward(car.x, car.y + CHASE_LOOK_HEIGHT, car.z);
}

class SceneHandlerImpl : public visualizer::SceneHandler {
public:
    SceneHandlerImpl(visualizer::Window* window)
        : SceneHandler(window) {
    }

    void onMousePress(Uint8 button) {
        switch (button) {
            case SDL_BUTTON_LEFT:
                world_time.forward();
                break;
            case SDL_BUTTON_MIDDLE:
                world_time.freeze();
                camera.set_velocity(0);
                break;
            case SDL_BUTTON_RIGHT:
                world_time.backward();
                break;
        }
    }

    void onMouseWheel(int horiz, int vert) {
        if (chase_mode)
            return;
        float v = camera.get_velocity();
        v += vert / 10.0f;
        camera.set_velocity(v);
    }

    void onMouseMotion(int dx, int dy) {
        static bool firstMotion = true;

        if (firstMotion) {
            // screen initialization
            firstMotion = false;
            return;
        }
        if (chase_mode)
            return; // mouse look is for the free-fly camera only

        float elevation = camera.get_elevation();
        float azimuth = camera.get_azimuth();

        elevation -= dy / 8.0f;
        azimuth += dx / 8.0f;

        elevation = fmax(-90.0f, elevation);
        elevation = fmin(90.0f, elevation);

        azimuth = fmod(azimuth, 360.0f);

        camera.look_at(azimuth, elevation);

        // printf("MOUSE MOTION: [%d %d], Camera [%f %f]\n", dx, dy, Camera.azimuth, Camera.elevation);
    }

    // DEALING WITH KEY PRESSES
    void onKeyDown(SDL_Keysym* keysym) {
        static bool wireframe = false;
        switch (keysym->sym) {

            case SDLK_HOME:
                camera.look_straight();
                break;

            case SDLK_PAGEDOWN:
                camera.update_altitude(-5.0f);
                break;

            case SDLK_PAGEUP:
                camera.update_altitude(5.0f);
                break;

            case SDLK_ESCAPE:
                terminate();
                break;

            // button C
            case SDLK_c:
                // toggle between the car chase camera
                // and the free fly camera
                chase_mode = !chase_mode;
                if (chase_mode) {
                    chase_init = false;
                } else {
                    camera.set_position(chase_pos.x, chase_pos.y, chase_pos.z);
                    camera.look_at(car.heading, -10.0f);
                    camera.set_velocity(0.0f);
                }
                break;

            case SDLK_r:
                reset_car();
                break;

            case SDLK_F12:
                if (anisotropic) {
                    anisotropic = false;
                } else {
                    anisotropic = true;
                }
                break;
            case SDLK_F10:
                if (wireframe) {
                    wireframe = false;
                } else {
                    wireframe = true;
                }
                if (wireframe) {
                    glDisable(GL_LIGHTING);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                } else {
                    glEnable(GL_LIGHTING);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                }

            default:
                break;
        }
    }
    void onEvent(SDL_Event* event) {
        switch (event->type) {
            case SDL_KEYDOWN:
                onKeyDown(&event->key.keysym);
                break;

            case SDL_MOUSEBUTTONDOWN:
                onMousePress(event->button.button);
                break;

            case SDL_MOUSEMOTION:
                onMouseMotion(event->motion.xrel, event->motion.yrel);
                break;

            case SDL_MOUSEWHEEL:
                onMouseWheel(event->wheel.x, event->wheel.y);
                break;
            case SDL_QUIT:
                terminate();
                break;

            default:
                break;
        }
    }
};

static GLint triangle;
static int _day = 1, _hour = 8, _minute = 0, _sec = 0;

int main(int argc, char* argv[]) {
    SDL_Event event;
    glewExperimental = GL_TRUE;
    glewInit();

    game::configuration::ScreenProperties screen_properties;
    game::configuration::types::screen screen = screen_properties.get();
    int flags = SDL_WINDOW_OPENGL;
    if (screen.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Configure the main screen
    visualizer::Window* window = visualizer::Window::createWindow(
        screen.title,
        screen.width,
        screen.height,
        flags,
        screen.depth,
        screen.antialiasing);

    // Create and configure the scene paramaters
    visualizer::Scene* scene1 = new visualizer::Scene(0, 0, screen.width, screen.height);
    scene1->lighting_enable();
    scene1->fov(screen.field_of_view);
    scene1->fpsrate(screen.target_fps);
    // visualizer::Model *kiaRio =new visualizer::Model(KIA_RIO_YELLOW, "data/cars/kiario/kia_rio.obj", 2);

    game::level::Initializer alps(scene1, "levels/alps", &world_time);

    // Add models
    // visualizer::Model *tyrol =new visualizer::Model(CLOUD_LAYER, "data/terrain/alpy2.obj", 1, 700.0f);
    // scene1->addModel(tyrol);

    terrain = new visualizer::Model(50, "data/alps1/al1.tri.obj", 1, 0.05f);
    playerCar = new visualizer::Model(51, "data/cars/_CORVETT.obj", 1, CAR_SCALE);
    terrain->positionInstance(0, 0.0f, 0.0f, -2700.0f);
    scene1->addModel(terrain);
    scene1->addModel(playerCar);
    reset_car();

    // Sky dome
    scene1->onRender([](visualizer::Scene* s) {
        static float x = -700.0f;
        static float y = -2.5f;
        static int hour = 11;
        static int minute = 0;
        static float dissipation = 0.001f;
        static int fps = 0;
        static time_t start = time(NULL);
        fps++;

        SDL_SetRelativeMouseMode(SDL_TRUE);

        time_t now = time(NULL);
        if (now - start >= 5) {
            float d = now - start;
            start = now;
            std::cout << "FPS: " << fps / d << std::endl;
            fps = 0;
        }
        dissipation += 0.0001;

        world_time.update();
        if (chase_mode) {
            float frame_dt = s->dt * 1e-6f; // Scene::dt is in microseconds
            if (frame_dt < 0.0f)
                frame_dt = 0.0f;
            if (frame_dt > 0.05f)
                frame_dt = 0.05f;
            drive_car(s, frame_dt);
        } else {
            camera.update();
            s->eye(camera.get_position());
            s->forward(camera.get_forward());
        }
        visualizer::SkyDome* sky = s->get_atmosphere();
        sky->update(world_time);
        visualizer::CloudLayer* clouds = s->get_cloud_layer().at(0);
        //		visualizer::CloudLayer *clouds2 = s->get_cloud_layer().at(1);
        //		visualizer::CloudLayer *clouds3 = s->get_cloud_layer().at(2);
        clouds->evolve(world_time.get_hour(), world_time.get_minute());
        //		clouds2->evolve(hour, minute);
        //		clouds3->evolve(hour, minute);

#if 0 // fog, needs scale of 1000.0f
		clouds->position(-1700.0f, 100.0f, -2350.0f);
#else // mid-alt clouds, scale==500.0f
      //	clouds->position(-680.0f, 500.0f, 0.0f);
#endif
        //		clouds2->position(300.0f, 500.0f, 0.0f);
        //		clouds3->position(-1680.0f, 500.0f, 0.0f);
        minute++;
        if (minute == 60)
            hour++;
        minute %= 60;
        hour %= 24;
        if (hour == 20)
            hour = 8;
        // sky->draw();
        // sky->increase_turbidity();
    });
    SceneHandlerImpl* sh = new SceneHandlerImpl(window);
    sh->addScene(scene1);
    sh->mainloop();

    return 0;
}
