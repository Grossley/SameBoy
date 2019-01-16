#include <Misc/uthash.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "wide_gb.h"

#define MAX(a,b) ((a) > (b) ? a : b)
#define MIN(a,b) ((a) < (b) ? a : b)

#define BACKGROUND_SIZE 256
#define WGB_YOUNG_SCENE_DELAY 2

// Forward declarations
WGB_scene *WGB_create_scene(wide_gb *wgb);
int hamming_distance(WGB_perceptual_hash x, WGB_perceptual_hash y);

/*---------------- Utils -------------------------------------------------*/

SDL_Point WGB_offset_point(SDL_Point point, SDL_Point offset)
{
    point.x += offset.x;
    point.y += offset.y;
    return point;
}

SDL_Rect WGB_offset_rect(SDL_Rect rect, int dx, int dy)
{
    rect.x += dx;
    rect.y += dy;
    return rect;
}

SDL_Rect WGB_scale_rect(SDL_Rect rect, double dx, double dy)
{
    rect.x *= dx;
    rect.y *= dy;
    rect.w *= dx;
    rect.h *= dy;
    return rect;
}

bool WGB_rect_contains_point(SDL_Rect rect, SDL_Point point)
{
    return (rect.x <= point.x
        && point.x <= rect.x + rect.w
        && rect.y <= point.y
        && point.y <= rect.y + rect.h);
}

bool WGB_rect_intersects_rect(SDL_Rect rect1, SDL_Rect rect2)
{
  if (rect2.x < rect1.x + rect1.w && rect1.x < rect2.x + rect2.w && rect2.y < rect1.y + rect1.h)
    return rect1.y < rect2.y + rect2.h;
  else
    return false;
}

bool WGB_tile_position_equal_to(WGB_tile_position position1, WGB_tile_position position2)
{
    return (position1.horizontal == position2.horizontal &&
            position1.vertical == position2.vertical);
}

WGB_tile_position WGB_tile_position_from_screen_point(wide_gb *wgb, SDL_Point screen_point)
{
    return (WGB_tile_position){
        .horizontal = floorf((wgb->active_scene->scroll.x + screen_point.x) / 160.0),
        .vertical   = floorf((wgb->active_scene->scroll.y + screen_point.y) / 144.0)
    };
}

SDL_Point WGB_tile_point_from_screen_point(wide_gb *wgb, SDL_Point screen_point, WGB_tile_position target_tile)
{
    SDL_Point tile_origin = {
        .x = wgb->active_scene->scroll.x - target_tile.horizontal * 160,
        .y = wgb->active_scene->scroll.y - target_tile.vertical   * 144
    };
    return WGB_offset_point(tile_origin, screen_point);
}

/*---------------- Initializers --------------------------------------*/

WGB_tile WGB_tile_init(WGB_tile_position position)
{
    WGB_tile new = {
        .position = position,
        .pixel_buffer = calloc(160 * 144, sizeof(uint32_t))
    };
    return new;
}

void WGB_tile_destroy(WGB_tile *tile)
{
    if (tile->pixel_buffer) {
        free(tile->pixel_buffer);
        tile->pixel_buffer = NULL;
    }
}

WGB_scene WGB_scene_init(int scene_id)
{
    WGB_scene new = {
        .id = scene_id,
        .scroll = { 0, 0 },
        .tiles_count = 0
    };
    new.created_at = time(NULL);
    return new;
}

void WGB_scene_destroy(WGB_scene *scene)
{
    for (int i = 0; i < scene->tiles_count; i++) {
        WGB_tile_destroy(&scene->tiles[i]);
    }
}

wide_gb WGB_init()
{
    wide_gb new = {
        .hardware_scroll = { 0, 0 },
        .window_rect = { 0, 0, 0, 0 },
        .window_enabled = false,
        .frame_perceptual_hash = 0,
        .previous_perceptual_hash = 0,
        .scene_frames = NULL,
        .find_existing_scene_countdown = 0
    };
    new.active_scene = WGB_create_scene(&new);
    return new;
}

void WGB_destroy(wide_gb *wgb)
{
    for (int i = 0; i < wgb->scenes_count; i++) {
        WGB_scene_destroy(&wgb->scenes[i]);
    }
}

/*---------------- Managing tiles -------------------------------------*/

size_t WGB_tiles_count(wide_gb *wgb)
{
    return wgb->active_scene->tiles_count;
}

WGB_tile *WGB_tile_at_position(wide_gb *wgb, WGB_tile_position position_to_find)
{
    size_t tiles_count = WGB_tiles_count(wgb);
    for (int i = 0; i < tiles_count; i++) {
        WGB_tile *tile = WGB_tile_at_index(wgb, i);
        if (WGB_tile_position_equal_to(tile->position, position_to_find)) {
            return tile;
        }
    }
    return NULL;
}

WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index)
{
    return &(wgb->active_scene->tiles[index]);
}

WGB_tile *WGB_create_tile(wide_gb *wgb, WGB_tile_position position)
{
// #if WIDE_GB_DEBUG
//     fprintf(stderr, "wgb: create tile at { %i, %i } (tiles count: %lu)\n", position.horizontal, position.vertical, wgb->active_scene->tiles_count);
// #endif
    WGB_scene *scene = wgb->active_scene;
    scene->tiles[scene->tiles_count] = WGB_tile_init(position);
    scene->tiles_count += 1;

   return &(scene->tiles[scene->tiles_count - 1]);
}

/*---------------- Managing scenes -------------------------------------*/

WGB_scene *WGB_create_scene(wide_gb *wgb)
{
    static int next_scene_id = 0;
#if WIDE_GB_DEBUG
    fprintf(stderr, "wgb: create scene %i\n", next_scene_id);
#endif

    wgb->scenes[wgb->scenes_count] = WGB_scene_init(next_scene_id);
    wgb->scenes_count += 1;
    next_scene_id += 1;

    return &(wgb->scenes[wgb->scenes_count - 1]);
}

WGB_scene *WGB_find_scene_by_id(wide_gb *wgb, int scene_id)
{
    for (int i = 0; i < wgb->scenes_count; i++) {
        if (wgb->scenes[i].id == scene_id) {
            return &wgb->scenes[i];
        }
    }
    return NULL;
}

WGB_scene_frame *WGB_find_scene_frame_for_hash(wide_gb *wgb, WGB_exact_hash hash)
{
    WGB_scene_frame *scene_frame;
    HASH_FIND_INT(wgb->scene_frames, &hash, scene_frame);
    return scene_frame;
}

void WGB_make_scene_active(wide_gb *wgb, WGB_scene *scene)
{
    wgb->active_scene = scene;
}

void WGB_delete_scene(wide_gb *wgb, WGB_scene *scene)
{
    #if WIDE_GB_DEBUG
        fprintf(stderr, "wgb: Delete scene %i.\n", scene->id);
    #endif

    // Remove the frames belonging to this scene
    WGB_scene_frame *current_scene_frame, *tmp;
    HASH_ITER(hh, wgb->scene_frames, current_scene_frame, tmp) {
        if (current_scene_frame->scene_id == scene->id) {
            HASH_DEL(wgb->scene_frames, current_scene_frame);
            free(current_scene_frame);
        }
    }

    // Destroy the scene itself
    WGB_scene_destroy(scene);
}

void WGB_restore_scene_for_frame(wide_gb *wgb, WGB_scene_frame *scene_frame)
{
    #if WIDE_GB_DEBUG
        fprintf(stderr, "wgb: Found a matching scene. Restore scene %i.\n", scene_frame->scene_id);
    #endif

    // Find the scene matching the frame
    WGB_scene *matched_scene = WGB_find_scene_by_id(wgb, scene_frame->scene_id);
    if (matched_scene == NULL) {
        fprintf(stderr, "wgb: Error while restoring a saved scene for scene %i: matching scene not found.\n", scene_frame->scene_id);
        return;
    }

    // Restore the scene
    matched_scene->scroll = scene_frame->scene_scroll;
    for (int i = 0; i < matched_scene->tiles_count; i++) {
        matched_scene->tiles[i].dirty = true;
    }
    WGB_scene *previous_scene = wgb->active_scene;
    WGB_make_scene_active(wgb, matched_scene);

    // Now that we know this frame belonged to a specific scene,
    // destroy the temporary scene that was created meanwhile.
    WGB_delete_scene(wgb, previous_scene);
}

void WGB_store_frame_hash(wide_gb *wgb, WGB_exact_hash hash)
{
    // Attempt to find an existing scene_frame for this frame
    WGB_scene_frame *scene_frame;
    HASH_FIND_INT(wgb->scene_frames, &hash, scene_frame);
    if (scene_frame == NULL) {
        // No scene_frame for this frame has been stored yet: create and insert it
        scene_frame = malloc(sizeof(WGB_scene_frame));
        scene_frame->frame_hash = hash;
        HASH_ADD_INT(wgb->scene_frames, frame_hash, scene_frame);
    }

    // Update the stored scene_frame with all informations (except the hash key)
    scene_frame->scene_id = wgb->active_scene->id;
    scene_frame->scene_scroll = wgb->active_scene->scroll;
}

double WGB_is_scene_young(WGB_scene *scene)
{
    return difftime(time(NULL), scene->created_at) < WGB_YOUNG_SCENE_DELAY;
}

void WGB_update_frame_hash(wide_gb *wgb, WGB_exact_hash hash)
{
    if (WGB_is_scene_young(wgb->active_scene)) {
        // Attempt to find an existing scene for this frame
        WGB_scene_frame *existing_frame = WGB_find_scene_frame_for_hash(wgb, hash);
        if (existing_frame && existing_frame->scene_id != wgb->active_scene->id) {
            WGB_restore_scene_for_frame(wgb, existing_frame);
        }
    }

    // Store the frame hash for the active scene
    WGB_store_frame_hash(wgb, hash);
}

WGB_tile* WGB_write_tile_pixel(wide_gb *wgb, SDL_Point pixel_pos, uint32_t pixel)
{
    // Retrieve the tile for this pixel
    WGB_tile_position tile_pos = WGB_tile_position_from_screen_point(wgb, pixel_pos);
    WGB_tile *tile = WGB_tile_at_position(wgb, tile_pos);

    // Create the tile if it doesn't exist
    if (tile == NULL) {
        tile = WGB_create_tile(wgb, tile_pos);
    }

    // Convert the pixel position from screen-space to tile-space
    SDL_Point pixel_destination = WGB_tile_point_from_screen_point(wgb, pixel_pos, tile_pos);

    tile->pixel_buffer[pixel_destination.x + pixel_destination.y * 160] = pixel;

    return tile;
}

void WGB_write_screen_pixels(wide_gb *wgb, uint32_t *pixels)
{
    // For each pixel visible on the console screen…
    for (int pixel_y = 0; pixel_y < 144; pixel_y++) {
        for (int pixel_x = 0; pixel_x < 160; pixel_x++) {
            SDL_Point pixel_pos = { pixel_x, pixel_y };
            if (wgb->window_enabled && WGB_rect_contains_point(wgb->window_rect, pixel_pos)) {
                continue; // pixel is in the window: skip it
            }
            // read the screen pixel
            uint32_t pixel = pixels[pixel_x + pixel_y * 160];
            // and write the pixel to the relevant tile
            WGB_tile *tile = WGB_write_tile_pixel(wgb, pixel_pos, pixel);
            tile->dirty = true;
        }
    }
}

/*---------------- Updates from hardware ------------------------------*/

void WGB_update_hardware_scroll(wide_gb *wide_gb, int scx, int scy)
{
    SDL_Point new_hardware_scroll = { scx, scy };

    // Compute difference with the previous scroll position
    SDL_Point delta;
    delta.x = new_hardware_scroll.x - wide_gb->hardware_scroll.x;
    delta.y = new_hardware_scroll.y - wide_gb->hardware_scroll.y;

    // Apply heuristic to tell if the background position wrapped into the other side
    const int fuzz = 10;
    const int threshold = BACKGROUND_SIZE - fuzz;
    // 255 -> 0 | delta.x is negative: we are going right
    // 0 -> 255 | delta.x is positive: we are going left
    if (abs(delta.x) > threshold) {
        if (delta.x < 0) delta.x += BACKGROUND_SIZE; // going right
        else             delta.x -= BACKGROUND_SIZE; // going left
    }
    // 255 -> 0 | delta.y is negative: we are going down
    // 0 -> 255 | delta.y is positive: we are going up
    if (abs(delta.y) > threshold) {
        if (delta.y < 0) delta.y += BACKGROUND_SIZE; // going down
        else             delta.y -= BACKGROUND_SIZE; // going up
    }

    // Update the new positions
    wide_gb->hardware_scroll = new_hardware_scroll;
    wide_gb->active_scene->scroll.x += delta.x;
    wide_gb->active_scene->scroll.y += delta.y;
}

void WGB_update_window_position(wide_gb *wgb, bool is_window_enabled, int wx, int wy)
{
    wgb->window_enabled = is_window_enabled;
    wgb->window_rect = (SDL_Rect) {
        .x = MIN(wx, 160),
        .y = MIN(wy, 144),
        .w = MAX(0, 160 - wx),
        .h = MAX(0, 144 - wy)
    };
}

void WGB_update_frame_perceptual_hash(wide_gb *wgb, WGB_perceptual_hash p_hash)
{
    wgb->previous_perceptual_hash = wgb->frame_perceptual_hash;
    wgb->frame_perceptual_hash = p_hash;

    const int scene_change_threshold = 12;
    int distance = hamming_distance(wgb->previous_perceptual_hash, wgb->frame_perceptual_hash);
#if WIDE_GB_DEBUG
    if (distance > 0) {
        fprintf(stderr, "WideGB scene distance: %i\n", distance);
    }
#endif

    if (distance >= scene_change_threshold) {
#if WIDE_GB_DEBUG
        fprintf(stderr, "\n\n\nWideGB scene changed (distance = %i)\n", distance);
#endif
        WGB_scene *previous_scene = wgb->active_scene;
        WGB_scene *new_scene = WGB_create_scene(wgb);
        WGB_make_scene_active(wgb, new_scene);

        // Short-lived scenes often contain garbage during screen transitions.
        // Delete it, so that we won't match it by error later.
        if (WGB_is_scene_young(previous_scene)) {
            WGB_delete_scene(wgb, previous_scene);
        }
    }
}

void WGB_update_screen(wide_gb *wgb, uint32_t *pixels, WGB_exact_hash hash, WGB_perceptual_hash p_hash)
{
    WGB_update_frame_perceptual_hash(wgb, p_hash);
    WGB_update_frame_hash(wgb, hash);
    WGB_write_screen_pixels(wgb, pixels);
}

/*---------------------- Laying out tiles --------------------------------*/

bool WGB_is_tile_visible(wide_gb *wgb, WGB_tile *tile, SDL_Rect viewport)
{
    SDL_Rect tile_rect = WGB_rect_for_tile(wgb, tile);
    return WGB_rect_intersects_rect(tile_rect, viewport);
}

SDL_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile)
{
    return (SDL_Rect) {
        .x = tile->position.horizontal * 160 - wgb->active_scene->scroll.x,
        .y = tile->position.vertical   * 144 - wgb->active_scene->scroll.y,
        .w = 160,
        .h = 144
    };
}

/*---------------------- Laying out screen -------------------------------*/

void WGB_get_screen_layout(wide_gb *wgb, SDL_Rect *bg_rect1, SDL_Rect *bg_rect2, SDL_Rect *wnd_rect)
{
    SDL_Rect window_rect = wgb->window_enabled ? wgb->window_rect : (SDL_Rect){ 160, 144, 0, 0 };

    bg_rect1->x = 0;
    bg_rect1->y = 0;
    bg_rect1->w = 160;
    bg_rect1->h = window_rect.y;

    bg_rect2->x = 0;
    bg_rect2->y = window_rect.y;
    bg_rect2->w = window_rect.x;
    bg_rect2->h = 144 - window_rect.y;

    wnd_rect->x = window_rect.x;
    wnd_rect->y = window_rect.y;
    wnd_rect->w = window_rect.w;
    wnd_rect->h = window_rect.h;
}

bool WGB_is_window_covering_screen(wide_gb *wgb, uint tolered_pixels)
{
    if (wgb->window_enabled) {
        return wgb->window_rect.x < tolered_pixels && wgb->window_rect.y < tolered_pixels;
    } else {
        return false;
    }
}

/*---------------- Frame hashing helpers -------------------------------*/

int hamming_distance(WGB_perceptual_hash x, WGB_perceptual_hash y) {
    WGB_perceptual_hash z  = x ^ y;
    int d = 0;
    for (; z > 0; z >>= 1) {
        d += z & 1;
    }
    return d;
}

WGB_exact_hash WGB_frame_hash(wide_gb *wgb, uint8_t *rgb_pixels)
{
    // FIXME: ignore pixels in window
    WGB_exact_hash hash = 0;
    for (int i = 0; i < 160 * 144 * 3; i += 3) {
        int pixels_sum = rgb_pixels[i] + rgb_pixels[i + 1] + rgb_pixels[i + 2];
        hash = (hash + 324723947 + pixels_sum * 2) ^ 93485734985;
    }
    return hash;
}

WGB_perceptual_hash WGB_added_difference_hash(wide_gb *wgb, uint8_t *rgb_pixels)
{
    const int block_length_h = 160 / 8;
    const int block_length_v = 144 / 8;
    const int block_size = block_length_h * block_length_v;

    // 1. Downsample to 8 * 8 blocks, and extract the luminance

    uint8_t grayscale[8*8];
    uint8_t r, g, b;
    // For each block
    for (int block_y = 0; block_y < 8; block_y++) {
        for (int block_x = 0; block_x < 8; block_x++) {
            float block_avg = 0;
            int block_top_x = block_x * block_length_h;
            int block_top_y = block_y * block_length_v;
            // For each pixel in the block
            for (int pixel_y = 0; pixel_y < block_length_v; pixel_y++) {
                for (int pixel_x = 0; pixel_x < block_length_h; pixel_x++) {
                    // Extract pixel color
                    int rgb_pos = ((block_top_x + pixel_x) + (block_top_y + pixel_y) * 160) * 3;
                    r = rgb_pixels[rgb_pos + 0];
                    g = rgb_pixels[rgb_pos + 1];
                    b = rgb_pixels[rgb_pos + 2];
                    // // Convert to grayscale
                    float grayscaled = 0.212671f * r + 0.715160f * g + 0.072169f * b;
                    // Add contribution to the block value
                    block_avg += (grayscaled / block_size);
                }
            }
            // Write final block value to the downsampled grayscale picture
            if (block_avg > 255) { block_avg = 255.0; }
            grayscale[block_x + block_y * 8] = (uint8_t)floor(block_avg);
        }
    }

    // 2. Count the number of blocks brighter than the block on the top-left

    int sum = 0;
    for (int x = 1; x < 8; x++) {
        for (int y = 1; y < 8; y++) {
            uint8_t block_luminance = grayscale[x + y * 8];
            uint8_t diagonal_block_luminance = grayscale[(x - 1) + (y - 1) * 8];
            if (block_luminance > diagonal_block_luminance) {
                sum += 1;
            }
        }
    }

    // 3. Encode the sum as a hash that can be compared using a hamming distance

    // The first N bits of the hash are set to 1, and the others to 0.
    WGB_perceptual_hash hash = 0;
    for (int i = 0; i < sum; i++) {
        hash |= 1ULL << i;
    }

    return hash;
}
