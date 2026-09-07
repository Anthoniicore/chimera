// SPDX-License-Identifier: GPL-3.0-only

#include "bitmaps.hpp"
#include "bitmap_lookup.hpp"
#include "../chimera.hpp"
#include "../signature/signature.hpp"
#include "../signature/hook.hpp"
#include "tag.hpp"
#include "tag_class.hpp"
#include "table.hpp"

namespace Chimera {

    Bitmap *get_bitmap_tag(TagID tag_id) noexcept {
        auto *tag = get_tag(tag_id);
        if(!tag || tag->primary_class != TAG_CLASS_BITMAP) {
            return nullptr;
        }
        return reinterpret_cast<Bitmap *>(tag->data);
    }

    TextureTable &TextureTable::get_texture_table() noexcept {
        static auto *pc_texture_table = **reinterpret_cast<TextureTable ***>(get_chimera().get_signature("pc_texture_table_sig").data() + 1);
        return *pc_texture_table;
    }

    void *bitmap_covert_format(BitmapData *bitmap, bool force_32) noexcept {
        void *converted_bitmap = nullptr;
        switch(bitmap->format) {
            case BITMAP_DATA_FORMAT_A8: {
                auto *pixel_data = reinterpret_cast<std::uint8_t *>(bitmap->base_address);
                if(force_32) {
                    std::uint32_t *a8r8g8b8_bitmap = reinterpret_cast<std::uint32_t *>(GlobalAlloc(0, bitmap->pixels_size * 4));
                    for(int i = 0; i < bitmap->pixels_size; i++) {
                        Pixel32 new_pixel = 0x00FFFFFF;
                        new_pixel = new_pixel | pixel_data[i] << 24;
                        a8r8g8b8_bitmap[i] = new_pixel;
                    }
                    converted_bitmap = reinterpret_cast<void *>(a8r8g8b8_bitmap);
                }
                else {
                    std::uint16_t *a8y8_bitmap = reinterpret_cast<std::uint16_t *>(GlobalAlloc(0, bitmap->pixels_size * 2));
                    for(int i = 0; i < bitmap->pixels_size; i++) {
                        a8y8_bitmap[i] = lookup_a8[pixel_data[i]];
                    }
                    converted_bitmap = reinterpret_cast<void *>(a8y8_bitmap);
                }
                break;
            }

            case BITMAP_DATA_FORMAT_AY8: {
                auto *pixel_data = reinterpret_cast<std::uint8_t *>(bitmap->base_address);
                if(force_32) {
                    std::uint32_t *a8r8g8b8_bitmap = reinterpret_cast<std::uint32_t *>(GlobalAlloc(0, bitmap->pixels_size * 4));
                    for(int i = 0; i < bitmap->pixels_size; i++) {
                        Pixel32 new_pixel = pixel_data[i] << 24;
                        new_pixel = new_pixel | pixel_data[i] << 16;
                        new_pixel = new_pixel | pixel_data[i] << 8;
                        new_pixel = new_pixel | pixel_data[i];

                        a8r8g8b8_bitmap[i] = new_pixel;
                    }
                    converted_bitmap = reinterpret_cast<void *>(a8r8g8b8_bitmap);
                }
                else {
                    std::uint16_t *a8y8_bitmap = reinterpret_cast<std::uint16_t *>(GlobalAlloc(0, bitmap->pixels_size * 2));
                    for(int i = 0; i < bitmap->pixels_size; i++) {
                        a8y8_bitmap[i] = lookup_ay8[pixel_data[i]];
                    }
                    converted_bitmap = reinterpret_cast<void *>(a8y8_bitmap);
                }
                break;
            }

            case BITMAP_DATA_FORMAT_Y8: {
                auto *pixel_data = reinterpret_cast<std::uint8_t *>(bitmap->base_address);
                std::uint32_t *a8r8g8b8_bitmap = reinterpret_cast<std::uint32_t *>(GlobalAlloc(0, bitmap->pixels_size * 4));
                for(int i = 0; i < bitmap->pixels_size; i++) {
                    Pixel32 new_pixel = 0xFF000000;
                    new_pixel = new_pixel | pixel_data[i] << 16;
                    new_pixel = new_pixel | pixel_data[i] << 8;
                    new_pixel = new_pixel | pixel_data[i];

                    a8r8g8b8_bitmap[i] = new_pixel;
                }
                converted_bitmap = reinterpret_cast<void *>(a8r8g8b8_bitmap);
                break;
            }

            case BITMAP_DATA_FORMAT_A8Y8: {
                auto *pixel_data = reinterpret_cast<std::uint16_t *>(bitmap->base_address);
                std::uint32_t *a8r8g8b8_bitmap = reinterpret_cast<std::uint32_t *>(GlobalAlloc(0, bitmap->pixels_size * 2));
                for(int i = 0; i < bitmap->pixels_size / 2; i++) {
                    Pixel32 new_pixel = pixel_data[i] << 16;
                    std::uint32_t y8 = pixel_data[i] << 24;
                    new_pixel = new_pixel | y8 >> 16;
                    new_pixel = new_pixel | y8 >> 24;

                    a8r8g8b8_bitmap[i] = new_pixel;
                }
                converted_bitmap = reinterpret_cast<void *>(a8r8g8b8_bitmap);
                break;
            }

            case BITMAP_DATA_FORMAT_P8_BUMP: {
                auto *pixel_data = reinterpret_cast<std::uint8_t *>(bitmap->base_address);
                std::uint32_t *a8r8g8b8_bitmap = reinterpret_cast<std::uint32_t *>(GlobalAlloc(0, bitmap->pixels_size * 4));
                for(int i = 0; i < bitmap->pixels_size; i++) {
                    a8r8g8b8_bitmap[i] = lookup_p8[pixel_data[i]];
                }
                converted_bitmap = reinterpret_cast<void *>(a8r8g8b8_bitmap);
                break;
            }

            default:
                break;
        }

        return converted_bitmap;
    }

}
