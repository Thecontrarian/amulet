#include <stdio.h>

#include "ft2build.h"
#include FT_FREETYPE_H

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"
#include "miniz.c"

#define MAX_SIZES 100
#define MAX_TEX_SIZE 2048
#define ADVANCE_SCALE 0.015625f

static FT_Library ft_library;
static FT_Face ft_face;
static char *png_filename;
static char *lua_filename;
static int font_sizes[MAX_SIZES];
static int *font_size_codepoints[MAX_SIZES];
static char *font_filenames[MAX_SIZES];
static int font_faces[MAX_SIZES];
static int num_fonts = 0;
static stbrp_rect *rects = NULL;
static int num_rects = 0;
static int bitmap_width = 128;
static int bitmap_height = 64;
static unsigned char *bitmap_data = NULL;
//static FT_Bitmap char_bitmap;
#define char_bitmap ft_face->glyph->bitmap

static void process_args(int argc, char *argv[]);
static void gen_rects();
static int try_pack();
static void write_data();
static void write_png();
static void load_font(int s);
static void load_char(int c);

int main(int argc, char *argv[]) {
    if (FT_Init_FreeType(&ft_library)) {
        fprintf(stderr, "error initializing freetype library\n");
        return EXIT_FAILURE;
    }
    //FT_Bitmap_New(&char_bitmap);
    process_args(argc, argv);
    gen_rects();
    while (!try_pack()) {
        if (bitmap_width == bitmap_height) {
            bitmap_width *= 2;
        } else {
            bitmap_height = bitmap_width;
        }
        if (bitmap_width > MAX_TEX_SIZE) {
            fprintf(stderr, 
                "max texture size of %d exceeded while trying to pack font\n", MAX_TEX_SIZE);
            return EXIT_FAILURE;
        }
    }
    bitmap_data = malloc(4 * bitmap_width * bitmap_height);
    memset(bitmap_data, 0, 4 * bitmap_width * bitmap_height);
    write_data();
    write_png();
    free(rects);
    free(bitmap_data);
    //FT_Bitmap_Done(ft_library, &char_bitmap);
    FT_Done_Face(ft_face);
    return EXIT_SUCCESS;
}

static void gen_rects() {
    int s;
    int c;
    int r;
    num_rects = 0;
    for (s = 0; s < num_fonts; s++) {
        c = 0;
        while (font_size_codepoints[s][c]) {
            num_rects++;
            c++;
        }
    }
    rects = malloc(sizeof(stbrp_rect) * num_rects);
    r = 0;
    for (s = 0; s < num_fonts; s++) {
        int sz = font_sizes[s];
        load_font(s);
        c = 0;
        while (font_size_codepoints[s][c]) {
            load_char(font_size_codepoints[s][c]);
            rects[r].id = 0;
            rects[r].w = char_bitmap.width + 1;
            rects[r].h = char_bitmap.rows + 1;
            r++;
            c++;
        }
    }
}

static int try_pack() {
    int r;
    stbrp_context ctx;
    stbrp_node *nodes = malloc(bitmap_width * sizeof(stbrp_node));
    stbrp_init_target(&ctx, bitmap_width-1, bitmap_height-1, nodes, bitmap_width);
    stbrp_pack_rects(&ctx, rects, num_rects);
    free(nodes);
    for (r = 0; r < num_rects; r++) {
        if (!rects[r].was_packed) {
            return 0;
        }
    }
    return 1;
}

static void write_data() {
    int r = 0;
    int s;
    int c;
    int i, j;
    int rows;
    int width;
    int pitch;
    unsigned char *src_ptr;
    unsigned char *dest_ptr;
    double tx, ty, tw, th, x1, y1, x2, y2;
    printf("local font_data = {\n");
    for (s = 0; s < num_fonts; s++) {
        int sz = font_sizes[s];
        load_font(s);
        printf("    [%d] = {\n", sz);
        c = 0;
        while (font_size_codepoints[s][c]) {
            int cp = font_size_codepoints[s][c];
            load_char(cp);
            rows = char_bitmap.rows;
            width = char_bitmap.width;
            pitch = char_bitmap.pitch;
            src_ptr = char_bitmap.buffer;
            dest_ptr = bitmap_data + (rects[r].x+1) * 4 +
                (rects[r].y+1) * bitmap_width * 4;
            if (char_bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                for (i = 0; i < rows; i++) {
                    for (j = 0; j < width; j++) {
                        dest_ptr[j*4+0] = 0xFF;
                        dest_ptr[j*4+1] = 0xFF;
                        dest_ptr[j*4+2] = 0xFF;
                        dest_ptr[j*4+3] = src_ptr[j];
                        /*
                        dest_ptr[j*4+0] = src_ptr[j];
                        dest_ptr[j*4+1] = src_ptr[j];
                        dest_ptr[j*4+2] = src_ptr[j];
                        dest_ptr[j*4+3] = 0xFF;
                        */
                    }
                    src_ptr += pitch;
                    dest_ptr += bitmap_width * 4;
                }
            } else if (char_bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                for (i = 0; i < rows; i++) {
                    for (j = 0; j < width; j++) {
                        dest_ptr[j*4+0] = 0xFF;
                        dest_ptr[j*4+1] = 0xFF;
                        dest_ptr[j*4+2] = 0xFF;
                        if (src_ptr[j>>3] & (128 >> (j&7))) {
                            dest_ptr[j*4+3] = 0xFF;
                        } else {
                            dest_ptr[j*4+3] = 0x00;
                        }
                        /*
                        dest_ptr[j*4+0] = src_ptr[j];
                        dest_ptr[j*4+1] = src_ptr[j];
                        dest_ptr[j*4+2] = src_ptr[j];
                        dest_ptr[j*4+3] = 0xFF;
                        */
                    }
                    src_ptr += pitch;
                    dest_ptr += bitmap_width * 4;
                }
            } else {
                fprintf(stderr, "unsupported pixel mode for codepoint %d: %d\n",
                    cp, char_bitmap.pixel_mode);
                exit(EXIT_FAILURE);
            }

            /* 
             * 1-4
             * |\|
             * 2-3
             */
            tx = ((double)rects[r].x + 0.5) / (double)bitmap_width;
            ty = 1.0 - (((double)rects[r].y) + 0.5) / (double)bitmap_height;
            tw = ((double)rects[r].w - 1.0) / (double)bitmap_width;
            th = ((double)rects[r].h - 1.0) / (double)bitmap_height;
            x1 = tx;
            y1 = ty - th;
            x2 = tx + tw;
            y2 = ty;
            printf("        [%d] = { "
                "tex_coords = {%g, %g, %g, %g, %g, %g, %g, %g}, "
                "advance = %g },\n",
                cp, 
                x1, y2, x1, y1, x2, y1, x2, y2,
                (double)ft_face->glyph->advance.x * ADVANCE_SCALE);
            r++;
            c++;
        }
        printf("    },\n");
    }
    printf("}\n\n");
    printf("return font_data\n");
}

static void write_png() {
    size_t len;
    void *png_data = tdefl_write_image_to_png_file_in_memory(
        bitmap_data, bitmap_width, bitmap_height, 4, &len);
    FILE *f = fopen(png_filename, "wb");
    fwrite(png_data, len, 1, f);
    fclose(f);
}

static int font_loaded = 0;

static void load_font(int s) {
    if (font_loaded) {
        FT_Done_Face(ft_face);
    }
    if (FT_New_Face(ft_library, font_filenames[s], font_faces[s], &ft_face)) {
        fprintf(stderr, "error loading font '%s'\n", font_filenames[s]);
        exit(EXIT_FAILURE);
    }
    if (FT_Set_Pixel_Sizes(ft_face, font_sizes[s], 0)) {
        fprintf(stderr, "unable to set size %d\n", font_sizes[s]);
        exit(EXIT_FAILURE);
    }
    font_loaded = 1;
}

static void load_char(int c) {
    if (FT_Load_Char(ft_face, c, FT_LOAD_TARGET_MONO)) {
        fprintf(stderr, "unable to load codepoint %d\n", c);
        exit(EXIT_FAILURE);
    }
    if (FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_MONO)) {
        fprintf(stderr, "unable to render codepoint %d\n", c);
        exit(EXIT_FAILURE);
    }
    /*
    if (FT_Bitmap_Convert(ft_library, &ft_face->glyph->bitmap, &char_bitmap, 1)) {
        fprintf(stderr, "error converting bitmap for codepoint %d\n", c);
        exit(EXIT_FAILURE);
    }
    */
}

#define start 0x20
#define end 0x7E

static void parse_spec(char *arg, int s) {
    // font.ttf#1@16:A-Z,a-z,0-9,0x20-0x2F,0x3A-0x40,0x5B-0x60,0x7B-0x7E
    int i;
    font_filenames[s] = arg;
    while (*arg != '\0' && *arg != '@' && *arg != '#') arg++;
check_size:
    switch (*arg) {
        case '\0':
            fprintf(stderr, "no size specified for font '%s'\n", font_filenames[s]);
            exit(EXIT_FAILURE);
            return;
        case '#':
            *arg = '\0';
            font_faces[s] = strtol(arg + 1, &arg, 10) - 1;
            if (font_faces[s] < 0) {
                fprintf(stderr, "font face must be positive\n");
                exit(EXIT_FAILURE);
            }
            goto check_size;
        case '@':
            *arg = '\0';
            font_sizes[s] = strtol(arg + 1, &arg, 10);
            break;
        default:
            fprintf(stderr, "unexpected character: '%c'\n", *arg);
            exit(EXIT_FAILURE);
            return;
    }
    if (*arg == '\0') {
        font_size_codepoints[s] = malloc(sizeof(int) * (end - start + 2));
        for (i = 0; i <= (end - start); i++) {
            font_size_codepoints[s][i] = i + start;
        }
        font_size_codepoints[s][end - start + 1] = 0;
        return;
    }
    if (*arg != ':') {
        fprintf(stderr, "unexpected character: '%c' (expecting ':')\n", *arg);
        exit(EXIT_FAILURE);
    }
}

static void process_args(int argc, char *argv[]) {
    /* myfont.png myfont.lua font.ttf#1@16:A-Z,a-z,0-9,0x20-0x2F,0x3A-0x40,0x5B-0x60,0x7B-0x7E */

    int i;
    if (argc < 4) {
        fprintf(stderr, "expecting at least 3 arguments\n");
        exit(EXIT_FAILURE);
    }

    png_filename = argv[1];
    lua_filename = argv[2];
    num_fonts = argc-3;
    for (i = 0; i < num_fonts; i++) {
        parse_spec(argv[i+3], i);
    }
}
