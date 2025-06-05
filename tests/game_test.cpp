#include "asr.h"

#include <utility>
#include <memory>
#include <tuple>
#include <chrono>

#include "SDL_mixer.h"

using namespace asr;

static const float CAMERA_SPEED{0.5f};
static const float CAMERA_SENSITIVITY{0.001f};

const glm::vec4 FORWARD{0.0f, 0.0f, 1.0f, 0.0f};
const glm::vec4 RIGHT{1.0f, 0.0f, 0.0f, 0.0f};

enum Game_State {
    Start_menu,
    Game,
    Win,
    Game_over
};

class Enemy {
public:
    typedef std::tuple<const std::string, unsigned int, unsigned int> enemy_sprite_data_type;

    enum State {
        Won,
        Alive,
        Dying,
        Dead
    };


    Enemy(const glm::vec3 &position, float size, float speed, const enemy_sprite_data_type& enemy_sprite_data_type)
            : _position{ position }, _speed(speed)
    {
        const auto& [sprite_file, sprite_frame_count, first_dying_state_sprite_frame] = enemy_sprite_data_type;

        auto [image_data, image_width, image_height, image_channels] = file_utilities::read_image_file(sprite_file);
        _texture = std::make_shared<ES2Texture>(image_data, image_width, image_height, image_channels);
        _texture->set_minification_filter(Texture::FilterType::Nearest);
        _texture->set_magnification_filter(Texture::FilterType::Nearest);
        _texture->set_mode(Texture::Mode::Modulation);
        _texture->set_transformation_enabled(true);
        _set_texture_frames(sprite_frame_count);
        _set_first_dying_texture_frame(first_dying_state_sprite_frame);

        auto [billboard_indices, billboard_vertices] = geometry_generators::generate_rectangle_geometry_data(4.0f, 4.0f, 3, 3);
        auto billboard_geometry = std::make_shared<ES2Geometry>(billboard_indices, billboard_vertices);
        auto billboard_materials = std::make_shared<ES2ConstantMaterial>();
        billboard_materials->set_texture_1(_texture);
        billboard_materials->set_blending_enabled(true);
        billboard_materials->set_face_culling_enabled(false);
        billboard_materials->set_transparent(true);

        _mesh = std::make_shared<Mesh>(billboard_geometry, billboard_materials);
        _mesh->set_position(position);
        _bounding_volume = Sphere{ _mesh->get_position(), size / 2 };
    }

    [[nodiscard]] const std::shared_ptr<Mesh> &get_mesh() const
    {
        return _mesh;
    }

    void set_target(const std::shared_ptr<Camera> &target)
    {
        _target = target;
    }

    void set_update_rate(int update_rate)
    {
        _update_rate = update_rate;
    }

    [[nodiscard]] std::pair<bool, float> interesect_with_ray(const Ray& ray) const
    {
        return std::make_pair(
                ray.intersects_with_sphere(_bounding_volume).first,
                ray.intersects_with_sphere(_bounding_volume).second
        );
    }

    void kill()
    {
        if (_state == Alive) {
            _state = Dying;
            _set_texture_frame(_first_dying_texture_frame);
        }
    }

    bool isDead() const
    {
        if (_state == Dead)
        {
            return true;
        }
        return false;
    }

    bool didWin() {
        if (_state == Won) {
            return true;
        }
        return false;
    }

    void update(float delta_time)
    {
        if (_state == Dying)
        {
            if (_update_request++ % _update_rate != 0) {
                return;

            }

            unsigned int frame = _texture_frame + 1;
            if (frame >= _texture_frames) {
                _state = Dead;
            }
            else {
                _set_texture_frame(frame);
            }
        }
        else if (_state == Alive) {
            if (_update_request++ % _update_rate != 0 || _target == nullptr)
            {
                return;
            }

            _set_texture_frame((_texture_frame + 1) % _first_dying_texture_frame);

            glm::vec3 target = _target->get_position();
            glm::vec3 position = _position;
            target.y = position.y;

            _velocity = glm::normalize(target - position) * 0.7f;
            _position += _velocity * 0.7f;
            if ((abs(abs(_position.x) - abs(target.x)) <= 0.4f) && (abs(abs(_position.z) - abs(target.z)) <= 0.2f)) {
                _state = Won;
            }

            _mesh->set_position(_position);
            _bounding_volume.set_center(_position);
        }

        _mesh->billboard_toward_camera(_target);
    }

private:
    State _state{ Alive };

    glm::vec3 _position;

    float _speed{ 1.0f };
    glm::vec3 _velocity{ 0.0f };

    std::shared_ptr<Mesh> _mesh;
    Sphere _bounding_volume{ glm::vec3{0.0f}, 1.0f };

    std::shared_ptr<Camera> _target{ nullptr };

    int _update_request{ 0 };
    int _update_rate{ 10 };

    std::shared_ptr<ES2Texture> _texture;
    unsigned int _texture_frame{ 0 };
    unsigned int _texture_frames{ 1 };
    unsigned int _first_dying_texture_frame{ 0 };

    void _set_texture_frame(unsigned int texture_frame)
    {
        _texture_frame = texture_frame;
        glm::mat4 matrix = _texture->get_transformation_matrix();
        matrix[3][0] = static_cast<float>(_texture_frame) / static_cast<float>(_texture_frames);
        _texture->set_transformation_matrix(matrix);
    }

    void _set_texture_frames(unsigned int texture_frames)
    {
        _texture_frames = texture_frames;
        glm::mat4 matrix = _texture->get_transformation_matrix();
        matrix[0][0] = 1.0f / static_cast<float>(_texture_frames);
        matrix[3][0] = static_cast<float>(_texture_frame) / static_cast<float>(_texture_frames);
        _texture->set_transformation_matrix(matrix);
    }

    void _set_first_dying_texture_frame(unsigned int first_dying_texture_frame)
    {
        _first_dying_texture_frame = first_dying_texture_frame;
    }
};

class Gun {
public:
    typedef std::tuple<const std::string, unsigned int> gun_sprite_data_type;

    enum State {
        Idling,
        Shooting
    };

    Gun(const glm::vec3& position, float gun_size, const glm::vec2& target, const gun_sprite_data_type& gun_sprite_data) : _target{ target }
    {
        const auto& [sprite_file, sprite_frame_count] = gun_sprite_data;

        auto [image2_data, image2_width, image2_height, image2_channels] = file_utilities::read_image_file(sprite_file);
        _texture = std::make_shared<ES2Texture>(image2_data, image2_width, image2_height, image2_channels);
        _texture->set_minification_filter(Texture::FilterType::Nearest);
        _texture->set_magnification_filter(Texture::FilterType::Nearest);
        _texture->set_mode(Texture::Mode::Modulation);
        _texture->set_transformation_enabled(true);
        _set_texture_frames(sprite_frame_count);

        // Changed generate_plane_geometry_data to generate_rectangle_geometry_data
        auto[overlay_indices, overlay_vertices] = geometry_generators::generate_rectangle_geometry_data(2, 2, 1, 1);
        auto overlay_geometry = std::make_shared<ES2Geometry>(overlay_indices, overlay_vertices);
        auto overlay_material = std::make_shared<ES2ConstantMaterial>();
        overlay_material->set_texture_1(_texture);
        overlay_material->set_blending_enabled(true);
        overlay_material->set_overlay(true);

        _mesh = std::make_shared<Mesh>(overlay_geometry, overlay_material);
        _mesh->set_position(position);
        _mesh->set_scale(glm::vec3(gun_size));
    }

    [[nodiscard]] const std::shared_ptr<Mesh>& get_mesh() const
    {
        return _mesh;
    }
    void set_point_of_view(const std::shared_ptr<Camera>& point_of_view)
    {
        _point_of_view = point_of_view;
    }

    void set_update_rate(int update_rate)
    {
        _update_rate = update_rate;
    }

    void update()
    {
        if (_state == Shooting)
        {
            if (_update_request++ % _update_rate != 0) {
                return;
            }

            unsigned int frame = _texture_frame + 1;
            if (frame >= _texture_frames) {
                _state = Idling;
                _set_texture_frame(0);
            }
            else {
                _set_texture_frame(frame);
            }
        }
        else if (_state == Idling) {
            _set_texture_frame(0);
        }
    }

    void shoot(const std::vector<std::shared_ptr<Enemy>>& enemies)
    {
        if (_state == Idling) {
            _state = Shooting;

            if (_point_of_view == nullptr) {
                return;
            }

            for (auto& enemy : enemies) {
                if (enemy->interesect_with_ray(_point_of_view->world_ray_from_screen_point(_target.x, _target.y)).first) {
                    bool enemyIsDead = enemy->isDead();
                    if (!enemyIsDead) {
                        enemy->kill();
                    }
                    return;
                }
            }
        }
    }

private:
    State _state{ Idling };

    std::shared_ptr<Mesh> _mesh;
    std::shared_ptr<Camera> _point_of_view;
    glm::vec2 _target;

    int _update_request{ 0 };
    int _update_rate{ 7 };


    std::shared_ptr<ES2Texture> _texture;
    unsigned int _texture_frame{ 0 };
    unsigned int _texture_frames{ 1 };

    void _set_texture_frame(unsigned int texture_frame)
    {
        _texture_frame = texture_frame;
        glm::mat4 matrix = _texture->get_transformation_matrix();
        matrix[3][0] = static_cast<float>(_texture_frame) / static_cast<float>(_texture_frames);
        _texture->set_transformation_matrix(matrix);
    }

    void _set_texture_frames(unsigned int texture_frames)
    {
        _texture_frames = texture_frames;

        glm::mat4 matrix = _texture->get_transformation_matrix();
        matrix[0][0] = 1.0f / static_cast<float>(_texture_frames);
        matrix[3][0] = static_cast<float>(_texture_frame) / static_cast<float>(_texture_frames);
        _texture->set_transformation_matrix(matrix);
    }
};

class Player {
public:

private:
    glm::vec3 _position;

    float _speed{ 1.1f };
    glm::vec3 _velocity{ 0.0f };

    std::shared_ptr<Mesh> _mesh;
    Sphere _bounding_volume{ glm::vec3{0.0f}, 1.0f };

    std::shared_ptr<Gun> _gun;
};

float randomFloat(float a, float b) {
    float random = ((float)rand()) / (float)RAND_MAX;
    float diff = b - a;
    float r = random * diff;
    return a + r;
}

[[noreturn]]
int main(int argc, char **argv)
{
    auto window = std::make_shared<ES2SDLWindow>("asr 2.0");
    window->set_capture_mouse_enabled(true);
    window->set_relative_mouse_mode_enabled(true);

//    // Initialize SDL Audio
//    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
//        printf("SDL could not initialize audio! SDL Error: %s\n", SDL_GetError());
//    }
//
//    // Initialize SDL_mixer
//    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
//        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
//    }
//
//    // Load and play background music
//    Mix_Music *background_music = Mix_LoadMUS("data/audio/E1M1.ogg");
//    if (background_music == nullptr) {
//        printf("Failed to load background music! SDL_mixer Error: %s\n", Mix_GetError());
//    } else {
//        // Play the music in an infinite loop (-1)
//        if (Mix_PlayMusic(background_music, -1) == -1) {
//            printf("Failed to play background music! SDL_mixer Error: %s\n", Mix_GetError());
//        }
//    }

    // Room Ground

    // Changed generate_plane_geometry_data to generate_rectangle_geometry_data
    auto [room_ground_indices, room_ground_vertices] = geometry_generators::generate_rectangle_geometry_data(50, 50, 1, 1);
    auto room_ground_geometry = std::make_shared<ES2Geometry>(room_ground_indices, room_ground_vertices);
    auto room_ground_material = std::make_shared<ES2PhongMaterial>();

    // Ninth Process
    auto [image_data1, image_width1, image_height1, image_channels1] = file_utilities::read_image_file("data/images/Ground.jpeg");
    auto room_ground_texture = std::make_shared<ES2Texture>(image_data1, image_width1, image_height1, image_channels1);

    room_ground_material->set_texture_1(room_ground_texture);
    room_ground_material->set_specular_exponent(2.0f);
    auto room_ground = std::make_shared<Mesh>(room_ground_geometry, room_ground_material);
    room_ground->set_position(glm::vec3(0.0f, -2.5f, 0.0f));
    room_ground->set_rotation(glm::vec3(-M_PI / 2.0f, 0.0f, 0.0f));

    // Starting to build the walls and roof for the game
    // Tenth Process with the outer part of the room for the roof
    float h = 50.0f * 0.2f;
    auto room_roof_material = std::make_shared<ES2PhongMaterial>();
    auto [image_data3, image_width3, image_height3, image_channels3] = file_utilities::read_image_file("data/images/nightSky.jpg");
    auto room_roof_texture = std::make_shared<ES2Texture>(image_data3, image_width3, image_height3, image_channels3);

    room_roof_material->set_texture_1(room_roof_texture);
    room_ground_material->set_specular_exponent(20.0f);
    auto room_roof = std::make_shared<Mesh>(room_ground_geometry, room_roof_material);
    room_roof->set_position(glm::vec3(0.0f, h - 2.5f, 0.0f));
    room_roof->set_rotation(glm::vec3(M_PI / 2.0f, 0.0f, 0.0f));

    // Walls
    auto wall_material = std::make_shared<ES2PhongMaterial>();
    auto [image_data2, image_width2, image_height2, image_channels2] = file_utilities::read_image_file("data/images/Wall.jpg");
    auto room_wall_texture = std::make_shared<ES2Texture>(image_data2, image_width2, image_height2, image_channels2);

    wall_material->set_texture_1(room_wall_texture);
    wall_material->set_specular_exponent(20.0f);
    // Left
    auto left_wall = std::make_shared<Mesh>(room_ground_geometry, wall_material);
    left_wall->set_position(glm::vec3(-25.0f, (h / 2.0f) - 2.5f, 0.0f));
    left_wall->set_rotation(glm::vec3(0.0f, M_PI / 2.0f, 0.0f));
    left_wall->add_to_scale_y(-0.75f);
    // Right
    auto right_wall = std::make_shared<Mesh>(room_ground_geometry, wall_material);
    right_wall->set_position(glm::vec3(25.0f, (h / 2.0f) - 2.5f, 0.0f));
    right_wall->set_rotation(glm::vec3(0.0f, -M_PI / 2.0f, 0.0f));
    right_wall->add_to_scale_y(-0.75f);
    // Start (behind)
    auto start_wall = std::make_shared<Mesh>(room_ground_geometry, wall_material);
    start_wall->set_position(glm::vec3(0.0f, (h / 2.0f) - 2.5f, 25.0f));
    start_wall->set_rotation(glm::vec3(0.0f, 9.425f, 0.0f));
    start_wall->add_to_scale_y(-0.75f);
    // End (accross)
    auto end_wall = std::make_shared<Mesh>(room_ground_geometry, wall_material);
    end_wall->set_position(glm::vec3(0.0f, (h / 2.0f) - 2.5f, -25.0f));
    end_wall->add_to_scale_y(-0.75f);

    // Monsters
    float enemies_size = 9;
    float enemies_speed = 10.01f;
    int enemies_sprite_frames = 10;
    int enemies_dying_first_sprite_frame = 4;
    std::tuple enemies_sprite_data =
            std::make_tuple(
                    "data/images/cacodemon.png",
                    enemies_sprite_frames,
                    enemies_dying_first_sprite_frame
            );

    glm::vec3 enemy1_position{ -5.0f, 1.5f, 0.0f };
    auto enemy1 = std::make_shared<Enemy>(enemy1_position, enemies_size, enemies_speed, enemies_sprite_data);

    glm::vec3 enemy2_position{ 5.0f, 1.5f, 0.0f };
    auto enemy2 = std::make_shared<Enemy>(enemy2_position, enemies_size, enemies_speed, enemies_sprite_data);

    std::vector<std::shared_ptr<Enemy>> enemies{ enemy1, enemy2 };

    // Gun
    glm::vec3 gun_position{ 0.0f, -1.0f, 0.0f };
    float gun_size = 2.0f;
    glm::vec2 gun_target{ window->get_width() / 2, window->get_height() / 2 };
    int gun_sprite_frames = 6;
    std::tuple gun_sprite_data = std::make_tuple("data/images/gun.png", gun_sprite_frames);

    auto gun = std::make_shared<Gun>(gun_position, gun_size, gun_target, gun_sprite_data);

    // Lamps
    auto [lamp_indices, lamp_vertices] = geometry_generators::generate_sphere_geometry_data(0.2f, 20, 20);
    auto lamp_sphere_geometry = std::make_shared<ES2Geometry>(lamp_indices, lamp_vertices);
    auto lamp_material = std::make_shared<ES2ConstantMaterial>();
    auto lamp1 = std::make_shared<Mesh>(lamp_sphere_geometry, lamp_material);
    auto lamp2 = std::make_shared<Mesh>(lamp_sphere_geometry, lamp_material);
    auto lamp3 = std::make_shared<Mesh>(lamp_sphere_geometry, lamp_material);
    auto lamp4 = std::make_shared<Mesh>(lamp_sphere_geometry, lamp_material);

    std::vector<std::shared_ptr<Object>> objects{
            room_ground,
            enemy1->get_mesh(),
            enemy2->get_mesh(),
            gun->get_mesh(),
            room_roof,
            left_wall,
            right_wall,
            end_wall,
            start_wall
    };

    auto scene = std::make_shared<Scene>(objects);

    // Point Lights
    auto point_light1 = std::make_shared<PointLight>();
    point_light1->set_intensity(2000.0f);
    point_light1->set_constant_attenuation(0.0f);
    point_light1->set_linear_attenuation(0.2f);
    point_light1->set_quadratic_attenuation(0.8f);
    point_light1->set_two_sided(true);
    point_light1->set_position(glm::vec3(-25.0f, 4.0f, 25.0f));
    point_light1->add_child(lamp1);
    scene->get_root()->add_child(point_light1);
    scene->get_point_lights().push_back(point_light1);

    auto point_light2 = std::make_shared<PointLight>();
    point_light2->set_intensity(2000.0f);
    point_light2->set_constant_attenuation(0.0f);
    point_light2->set_linear_attenuation(0.2f);
    point_light2->set_quadratic_attenuation(0.8f);
    point_light2->set_two_sided(true);
    point_light2->set_position(glm::vec3(25.0f, 4.0f, 25.0f));
    point_light2->add_child(lamp2);
    scene->get_root()->add_child(point_light2);
    scene->get_point_lights().push_back(point_light2);

    auto point_light3 = std::make_shared<PointLight>();
    point_light3->set_intensity(2000.0f);
    point_light3->set_constant_attenuation(0.0f);
    point_light3->set_linear_attenuation(0.2f);
    point_light3->set_quadratic_attenuation(0.8f);
    point_light3->set_two_sided(true);
    point_light3->set_position(glm::vec3(25.0f, 4.0f, -25.0f));
    point_light3->add_child(lamp3);
    scene->get_root()->add_child(point_light3);
    scene->get_point_lights().push_back(point_light3);

    auto point_light4 = std::make_shared<PointLight>();
    point_light4->set_intensity(2000.0f);
    point_light4->set_constant_attenuation(0.0f);
    point_light4->set_linear_attenuation(0.2f);
    point_light4->set_quadratic_attenuation(0.8f);
    point_light4->set_two_sided(true);
    point_light4->set_position(glm::vec3(-25.0f, 4.0f, -25.0f));
    point_light4->add_child(lamp4);
    scene->get_root()->add_child(point_light4);
    scene->get_point_lights().push_back(point_light4);

    // Twelfth Process
    // From here the game screen is gonna start
    // Changed generate_plane_geometry_data to generate_rectangle_geometry_data
    auto [rect_indices, rect_vertices] = geometry_generators::generate_rectangle_geometry_data(6, 4, 1, 1);
    auto rect_geometry = std::make_shared<ES2Geometry>(rect_indices, rect_vertices);

    auto start_rect_material = std::make_shared<ES2ConstantMaterial>();
    start_rect_material->set_emission_color(glm::vec4(1.0f));
    auto [image_data, image_width, image_height, image_channels] = file_utilities::read_image_file("data/images/Welcome.jpg");
    auto start_texture = std::make_shared<ES2Texture>(image_data, image_width, image_height, image_channels);
    start_rect_material->set_texture_1(start_texture);
    start_rect_material->set_face_culling_enabled(false);
    start_rect_material->set_blending_enabled(true);
    start_rect_material->set_transparent(true);
    start_rect_material->set_overlay(true);

    auto start_rect = std::make_shared<Mesh>(rect_geometry, start_rect_material);
    start_rect->set_position(glm::vec3(0.0f, 0.0f, 0.0f));

    std::vector<std::shared_ptr<Object>> start_object{ start_rect };
    auto scene1 = std::make_shared<Scene>(start_object);

    auto win_rect_material = std::make_shared<ES2ConstantMaterial>();
    win_rect_material->set_emission_color(glm::vec4(1.0f));
    auto [image_data0, image_width0, image_height0, image_channels0] = file_utilities::read_image_file("data/images/Winner.jpg");
    auto win_texture = std::make_shared<ES2Texture>(image_data0, image_width0, image_height0, image_channels0);
    win_rect_material->set_texture_1(win_texture);
    win_rect_material->set_face_culling_enabled(false);
    win_rect_material->set_blending_enabled(true);
    win_rect_material->set_transparent(true);
    win_rect_material->set_overlay(true);

    auto win_rect = std::make_shared<Mesh>(rect_geometry, win_rect_material);
    win_rect->set_position(glm::vec3(0.0f, 0.0f, 0.0f));

    std::vector<std::shared_ptr<Object>> win_object{ win_rect };
    auto scene2 = std::make_shared<Scene>(win_object);

    auto game_over_rect_material = std::make_shared<ES2ConstantMaterial>();
    game_over_rect_material->set_emission_color(glm::vec4(1.0f));
    auto [image_data4, image_width4, image_height4, image_channels4] = file_utilities::read_image_file("data/images/GameOver.jpeg");
    auto game_over_texture = std::make_shared<ES2Texture>(image_data4, image_width4, image_height4, image_channels4);
    game_over_rect_material->set_texture_1(game_over_texture);
    game_over_rect_material->set_face_culling_enabled(false);
    game_over_rect_material->set_blending_enabled(true);
    game_over_rect_material->set_transparent(true);
    game_over_rect_material->set_overlay(true);

    auto game_over_rect = std::make_shared<Mesh>(rect_geometry, game_over_rect_material);
    game_over_rect->set_position(glm::vec3(0.0f, 0.0f, 0.0f));

    std::vector<std::shared_ptr<Object>> game_over_object{ game_over_rect };
    auto scene3 = std::make_shared<Scene>(game_over_object);

    // Camera
    auto camera = scene->get_camera();
    camera->set_position(glm::vec3(0.0f, 0.0f, 20.0f));
    camera->set_zoom(3.0f);

    enemy1->set_target(camera);
    enemy2->set_target(camera);

    gun->set_point_of_view(camera);
    Game_State game_state{ Start_menu };

    auto prev_frame_time = std::chrono::high_resolution_clock::now();

    // Input
    window->set_on_late_keys_down([&](const uint8_t* keys) {
        glm::mat4 model_matrix = camera->get_model_matrix();
        if (game_state == Start_menu) {
            if (keys[SDL_SCANCODE_ESCAPE]) {
                exit(0);
            }
            if (keys[SDL_SCANCODE_SPACE]) {
                game_state = Game;
                prev_frame_time = std::chrono::high_resolution_clock::now();
            }
        }
        else if (game_state == Game) {
            if (keys[SDL_SCANCODE_W]) {
                if (camera->get_position().x >= -24.0f &&
                    camera->get_position().x <= 24.0f &&
                    camera->get_position().z >= -24.0f &&
                    camera->get_position().z <= 24.0f)
                {
                    camera->add_to_position(-glm::vec3(model_matrix * FORWARD * CAMERA_SPEED));
                }
                else
                {
                    camera->add_to_position(glm::vec3(model_matrix * FORWARD * 2.0f));
                }
            }
            if (keys[SDL_SCANCODE_A]) {
                if (camera->get_position().x >= -24.0f &&
                    camera->get_position().x <= 24.0f &&
                    camera->get_position().z >= -24.0f &&
                    camera->get_position().z <= 24.0f)
                {
                    camera->add_to_position(-glm::vec3(model_matrix * RIGHT * CAMERA_SPEED));
                }
                else
                {
                    camera->add_to_position(glm::vec3(model_matrix * RIGHT * 2.0f));
                }
            }
            if (keys[SDL_SCANCODE_S]) {
                if (camera->get_position().x >= -24.0f &&
                    camera->get_position().x <= 24.0f &&
                    camera->get_position().z >= -24.0f &&
                    camera->get_position().z <= 24.0f)
                {
                    camera->add_to_position(glm::vec3(model_matrix * FORWARD * CAMERA_SPEED));
                }
                else
                {
                    camera->add_to_position(-glm::vec3(model_matrix * FORWARD * 2.0f));
                }
            }
            if (keys[SDL_SCANCODE_D]) {
                if (camera->get_position().x >= -24.0f &&
                    camera->get_position().x <= 24.0f &&
                    camera->get_position().z >= -24.0f &&
                    camera->get_position().z <= 24.0f)
                {
                    camera->add_to_position(glm::vec3(model_matrix * RIGHT * CAMERA_SPEED));
                }
                else
                {
                    camera->add_to_position(-glm::vec3(model_matrix * RIGHT * 2.0f));
                }
            }
            if (keys[SDL_SCANCODE_ESCAPE]) {
                exit(0);
            }
        }
        else if (game_state == Win) {
            if (keys[SDL_SCANCODE_ESCAPE]) {
                exit(0);
            }
            if (keys[SDL_SCANCODE_SPACE]) {
                exit(0);
            }
        }
        else if (game_state == Game_over) {
            if (keys[SDL_SCANCODE_ESCAPE]) {
                exit(0);
            }
            if (keys[SDL_SCANCODE_SPACE]) {
                exit(0);
            }
        }
    });

    window->set_on_mouse_move([&](int x, int y, int x_rel, int y_rel) {
        if (game_state == Game) {
            camera->add_to_rotation_y(static_cast<float>(-x_rel) * CAMERA_SENSITIVITY);
        }
    });

    window->set_on_mouse_down([&](int botton, int x, int y) {
        if (game_state == Game) {
            gun->shoot(enemies);
        }
    });

    // Rendering
    float add_enemy_time_count = 0;
    int enemy_count = 0;
    int enemies_total = 5;

    ES2Renderer renderer(scene, window);
    ES2Renderer renderer1(scene1, window);
    ES2Renderer renderer2(scene2, window);
    ES2Renderer renderer3(scene3, window);

    for (;;) {
        window->poll();

        if (game_state == Start_menu) {
            renderer1.render();
        }
        else if (game_state == Game) {

            auto current_frame_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> time_span = std::chrono::duration_cast<std::chrono::duration<float>>(current_frame_time - prev_frame_time);
            float delta_time = time_span.count();

            add_enemy_time_count += delta_time;
            if (add_enemy_time_count >= 5.0 && enemy_count <= 3) {
                glm::vec3 enemy_position{ randomFloat(-25.0f, 25.0f), 1.5f, randomFloat(-25.0f, 15.0f) };
                auto enemy = std::make_shared<Enemy>(enemy_position, enemies_size, enemies_speed, enemies_sprite_data);
                enemy->set_target(camera);
                enemies.push_back(enemy);
                scene->get_root()->add_child(enemy->get_mesh());
                add_enemy_time_count = 0;
                ++enemy_count;
            }
            int cx = static_cast<int>(window->get_width() / 2);
            int cy = static_cast<int>(window->get_height() / 2);

            Ray ray = camera->world_ray_from_screen_point(cx, cy);
            for (auto& enemy : enemies)
            {
                std::pair inter = enemy->interesect_with_ray(ray);
                enemy->update(delta_time);
                if (enemy->didWin() && !enemy->isDead()) {
                    game_state = Game_over;
                }
            }
            auto t = 0;

            for (auto& enemy : enemies)
            {
                if (enemy->isDead()) {
                    t++;
                }
            }
            if (t == enemies_total) {
                game_state = Win;

            }
            gun->update();
            renderer.render();
            prev_frame_time = current_frame_time;

        }
        else if (game_state == Win) {
            renderer2.render();
        }
        else if (game_state == Game_over) {
            renderer3.render();
        }
    }
}
