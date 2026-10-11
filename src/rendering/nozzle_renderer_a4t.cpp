// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_a4t.cpp
/// @brief A4T toolhead renderer implementation
///
/// Traced from A4T SVG — polygon-based rendering using LVGL triangle
/// primitives with ear-clipping triangulation, matching the approach
/// of nozzle_renderer_stealthburner.cpp.

#include "nozzle_renderer_a4t.h"

#include "nozzle_renderer_common.h"

#include <cmath>
#include <iterator>

// ============================================================================
// Polygon Data (design space: width=1000, height=1334)
// Traced from A4T SVG (viewBox 0 0 766 1021)
// Visual center measured from bounding box: X=500, Y=667
// ============================================================================

static constexpr int DESIGN_CENTER_X = 500;
static constexpr int DESIGN_CENTER_Y = 630;

// --- Silhouette (overall outline) ---
static const lv_point_t pts_silhouette[] = {
    {135, 673},  {125, 1169}, {172, 1174}, {255, 1284}, {370, 1289}, {386, 1263}, {386, 1211},
    {433, 1169}, {563, 1169}, {616, 1216}, {610, 1253}, {621, 1258}, {626, 1289}, {684, 1279},
    {720, 1289}, {757, 1274}, {819, 1180}, {887, 1169}, {877, 668},  {819, 657},  {819, 626},
    {793, 600},  {798, 522},  {819, 501},  {866, 485},  {866, 349},  {819, 308},  {819, 292},
    {845, 271},  {840, 250},  {746, 161},  {757, 104},  {704, 93},   {668, 114},  {668, 83},
    {642, 78},   {642, 0},    {548, 0},    {553, 78},   {522, 83},   {516, 140},  {469, 135},
    {454, 140},  {459, 156},  {396, 151},  {365, 167},  {328, 271},  {334, 323},  {323, 328},
    {318, 360},  {328, 370},  {328, 469},  {214, 501},  {214, 610},  {198, 616},  {198, 652},
};

// Nozzle / copper area silhouette (separate piece below body)
static const lv_point_t pts_silhouette_nozzle[] = {
    {412, 1211}, {433, 1221}, {433, 1274}, {443, 1279}, {443, 1289}, {475, 1295},
    {496, 1310}, {506, 1331}, {516, 1305}, {527, 1305}, {532, 1295}, {563, 1295},
    {563, 1284}, {574, 1279}, {574, 1237}, {590, 1211}, {558, 1190}, {433, 1190},
};

// --- Body (main gray surface) ---
static const lv_point_t pts_body_0[] = {
    {168, 1006}, {171, 1140}, {211, 1138}, {315, 1282}, {351, 1285}, {387, 1211}, {421, 1176},
    {484, 1168}, {585, 1180}, {644, 1284}, {679, 1283}, {784, 1138}, {831, 1149}, {832, 1028},
    {845, 1024}, {866, 1077}, {883, 1063}, {862, 882},  {846, 1007}, {813, 987},  {805, 846},
    {808, 671},  {874, 668},  {820, 656},  {815, 620},  {793, 603},  {766, 613},  {803, 658},
    {787, 677},  {745, 630},  {708, 616},  {648, 618},  {599, 659},  {395, 656},  {347, 617},
    {291, 616},  {257, 626},  {209, 676},  {196, 659},  {210, 645},  {212, 638},  {192, 656},
    {142, 665},  {138, 674},  {188, 668},  {193, 680},  {195, 995},  {185, 1032}, {186, 1006},
};

static const lv_point_t pts_body_1[] = {
    {331, 269}, {328, 371}, {388, 429}, {373, 468}, {377, 477}, {313, 481}, {216, 503}, {217, 546},
    {586, 541}, {794, 548}, {793, 521}, {805, 506}, {803, 500}, {823, 488}, {868, 445}, {871, 435},
    {867, 402}, {861, 397}, {855, 425}, {838, 457}, {787, 499}, {686, 480}, {654, 479}, {613, 444},
    {599, 442}, {613, 464}, {610, 498}, {593, 513}, {563, 510}, {550, 493}, {550, 461}, {577, 443},
    {578, 437}, {551, 433}, {518, 398}, {517, 316}, {537, 297}, {603, 261}, {638, 172}, {633, 179},
    {584, 163}, {528, 217}, {507, 218}, {394, 324}, {357, 270}, {344, 226},
};

static const lv_point_t pts_body_2[] = {
    {805, 270}, {740, 164}, {729, 194}, {775, 267}, {781, 284}, {781, 316}, {777, 333}, {752, 363},
    {753, 366}, {763, 364}, {775, 366}, {780, 362}, {784, 364}, {764, 400}, {772, 398}, {823, 364},
    {818, 361}, {815, 342}, {811, 340}, {808, 333}, {811, 329}, {809, 272},
};

// --- Surface detail (mid-gray) ---
static const lv_point_t pts_detail_0[] = {
    {459, 150}, {462, 158}, {453, 160}, {422, 150}, {409, 150}, {375, 158}, {366, 167}, {344, 227},
    {355, 270}, {392, 327}, {400, 324}, {508, 220}, {563, 217}, {585, 166}, {636, 180}, {644, 159},
    {654, 160}, {650, 150}, {651, 145}, {655, 140}, {663, 140}, {666, 131}, {647, 124}, {641, 113},
    {632, 107}, {597, 100}, {583, 107}, {573, 120}, {528, 137}, {540, 140}, {572, 129}, {561, 160},
    {530, 156}, {534, 152}, {548, 154}, {553, 147}, {483, 146},
};

static const lv_point_t pts_detail_1[] = {
    {196, 655}, {195, 661}, {200, 674}, {209, 678}, {215, 675}, {229, 657}, {264, 623}, {290, 618},
    {344, 618}, {359, 624}, {406, 670}, {417, 675}, {442, 678}, {573, 678}, {592, 669}, {634, 627},
    {652, 618}, {701, 618}, {715, 618}, {737, 625}, {785, 679}, {800, 677}, {805, 661}, {797, 646},
    {768, 615}, {790, 608}, {733, 598}, {681, 597}, {650, 597}, {632, 602}, {581, 651}, {569, 656},
    {547, 658}, {424, 655}, {409, 645}, {373, 607}, {357, 601}, {286, 598}, {222, 606}, {218, 604},
    {209, 611}, {231, 612}, {232, 614},
};

static const lv_point_t pts_detail_2[] = {
    {329, 322}, {324, 331}, {330, 337}, {321, 344}, {326, 353}, {320, 364}, {327, 371}, {320, 380},
    {326, 388}, {322, 396}, {325, 401}, {334, 403}, {327, 414}, {339, 418}, {335, 429}, {346, 433},
    {343, 445}, {357, 445}, {353, 457}, {357, 460}, {368, 457}, {366, 473}, {323, 474}, {220, 497},
    {215, 502}, {215, 549}, {218, 547}, {219, 504}, {320, 482}, {426, 480}, {426, 476}, {422, 474},
    {378, 473}, {376, 468}, {390, 429}, {332, 370}, {333, 323},
};

// --- Dark features (fan opening, screw holes, background cutouts) ---
static const lv_point_t pts_fan_opening[] = {
    {370, 763}, {352, 797}, {348, 821}, {357, 825}, {362, 856}, {380, 907}, {388, 922},
    {405, 942}, {442, 969}, {479, 980}, {537, 977}, {572, 959}, {603, 932}, {617, 912},
    {631, 867}, {634, 863}, {642, 827}, {645, 797}, {628, 764}, {616, 746}, {583, 717},
    {572, 714}, {572, 719}, {588, 742}, {587, 744}, {545, 721}, {502, 713}, {476, 715},
    {449, 723}, {410, 747}, {421, 728}, {440, 708}, {439, 705}, {415, 717}, {398, 731},
};

static const lv_point_t pts_dark_mechanism[] = {
    {621, 445}, {653, 475}, {790, 498}, {835, 464}, {857, 426}, {862, 397}, {831, 360}, {774, 394},
    {774, 383}, {788, 363}, {755, 362}, {778, 333}, {779, 285}, {773, 275}, {757, 273}, {756, 298},
    {743, 325}, {731, 329}, {731, 291}, {746, 285}, {752, 258}, {748, 228}, {739, 214}, {739, 250},
    {731, 266}, {724, 270}, {710, 247}, {698, 280}, {703, 328}, {677, 382}, {624, 406}, {625, 414},
    {629, 407}, {646, 403}, {650, 411}, {622, 425},
};

// Screw holes
static const lv_point_t pts_screw_left[] = {
    {303, 1174}, {318, 1177}, {334, 1173}, {347, 1162}, {354, 1146}, {354, 1131}, {349, 1118},
    {339, 1108}, {313, 1103}, {297, 1109}, {287, 1120}, {282, 1137}, {283, 1147}, {291, 1165},
};

static const lv_point_t pts_screw_right[] = {
    {649, 1164}, {662, 1173}, {676, 1177}, {696, 1172}, {707, 1165}, {714, 1146}, {715, 1131},
    {712, 1124}, {699, 1110}, {685, 1102}, {658, 1108}, {646, 1122}, {642, 1133}, {642, 1143},
};

// --- Highlights ---
static const lv_point_t pts_highlight_top_left[] = {
    {517, 134}, {458, 139}, {455, 143}, {456, 157}, {407, 148}, {384, 157}, {421, 152}, {454, 162},
    {471, 157}, {463, 155}, {464, 151}, {483, 148}, {553, 148}, {553, 142}, {549, 139}, {520, 135},
    {525, 110}, {548, 106}, {551, 78},  {550, 1},   {548, 76},  {521, 77},  {520, 88},  {513, 90},
};

static const lv_point_t pts_highlight_top_right[] = {
    {642, 76},  {640, 0},   {638, 78}, {643, 106}, {663, 109}, {669, 121},
    {683, 107}, {712, 97},  {721, 98}, {756, 110}, {751, 104}, {715, 91},
    {682, 105}, {676, 105}, {676, 93}, {669, 87},  {668, 77},
};

// Voron cube logo highlights
static const lv_point_t pts_highlight_cube_l[] = {
    {459, 1106}, {467, 1113}, {492, 1126}, {494, 1086}, {463, 1067}, {458, 1068}, {457, 1095},
};

static const lv_point_t pts_highlight_cube_r[] = {
    {536, 1066}, {504, 1085}, {503, 1126}, {506, 1127}, {539, 1107}, {540, 1070},
};

static const lv_point_t pts_highlight_cube_top[] = {
    {498, 1079}, {532, 1058}, {530, 1054}, {501, 1038}, {493, 1039}, {464, 1055}, {474, 1065},
};

// --- Green shadow (darkest green tone) ---
static const lv_point_t pts_green_shadow_0[] = {
    {627, 382}, {624, 426}, {649, 416}, {656, 409}, {651, 408}, {649, 400},
    {677, 384}, {685, 371}, {705, 328}, {705, 309}, {699, 280}, {685, 309},
};

static const lv_point_t pts_green_shadow_1[] = {
    {439, 705}, {395, 759}, {392, 769}, {421, 742}, {450, 725}, {476, 717}, {502, 715}, {536, 720},
    {552, 726}, {584, 746}, {606, 767}, {573, 713}, {558, 704}, {529, 696}, {485, 693}, {450, 700},
};

static const lv_point_t pts_green_shadow_band[] = {
    {789, 543}, {350, 539}, {337, 542}, {217, 544}, {216, 547}, {218, 550},
    {226, 551}, {362, 546}, {631, 546}, {792, 551}, {795, 548},
};

// --- Green dark ---
static const lv_point_t pts_green_dark_0[] = {
    {607, 439}, {623, 444}, {628, 382}, {690, 305}, {754, 133}, {751, 130},
    {708, 226}, {690, 273}, {690, 282}, {634, 354}, {621, 366}, {589, 411},
    {581, 417}, {564, 421}, {585, 423}, {586, 428}, {581, 437},
};

static const lv_point_t pts_green_dark_1[] = {
    {763, 177}, {753, 174}, {808, 272}, {809, 330}, {814, 335}, {828, 334}, {837, 340}, {848, 359},
    {845, 368}, {862, 388}, {863, 354}, {857, 337}, {815, 304}, {818, 290}, {838, 269}, {839, 256},
};

static const lv_point_t pts_green_dark_band[] = {
    {403, 607}, {370, 605}, {370, 608}, {409, 647}, {424, 657}, {444, 659},
    {547, 660}, {577, 656}, {587, 649}, {625, 611}, {614, 607}, {608, 613},
    {594, 613}, {564, 647}, {551, 654}, {449, 654}, {435, 646},
};

// --- Bright green (A4T signature color) ---
static const lv_point_t pts_green_bright_band[] = {
    {646, 545}, {219, 549}, {215, 556}, {213, 569}, {215, 606}, {286, 600}, {338, 601},
    {383, 606}, {403, 609}, {434, 647}, {446, 655}, {543, 656}, {552, 656}, {566, 649},
    {595, 614}, {609, 615}, {616, 609}, {624, 611}, {634, 603}, {649, 599}, {732, 599},
    {754, 603}, {794, 604}, {794, 553}, {792, 550},
};

static const lv_point_t pts_green_bright_top[] = {
    {524, 326}, {524, 393}, {555, 424}, {572, 422}, {586, 416}, {692, 282}, {692, 273},
    {709, 226}, {723, 200}, {750, 134}, {754, 133}, {756, 127}, {756, 110}, {715, 95},
    {682, 106}, {674, 112}, {653, 159}, {614, 264}, {537, 308}, {528, 317},
};

static const lv_point_t pts_green_bright_ptfe[] = {
    {549, 77},  {540, 133}, {574, 122}, {585, 108}, {597, 102},
    {635, 111}, {646, 125}, {640, 78},  {639, 1},   {550, 0},
};

// --- Copper nozzle ---
static const lv_point_t pts_copper[] = {
    {465, 1194}, {436, 1191}, {428, 1196}, {428, 1214}, {433, 1228}, {435, 1271},
    {446, 1290}, {453, 1294}, {496, 1298}, {541, 1295}, {546, 1301}, {550, 1295},
    {560, 1291}, {574, 1271}, {574, 1232}, {581, 1217}, {586, 1214}, {574, 1199},
    {564, 1193}, {555, 1193}, {533, 1197}, {472, 1198},
};

namespace {
enum : uint8_t {
    SILHOUETTE,
    BODY,
    DETAIL,
    DARK,
    HIGHLIGHT,
    GREEN_SHADOW,
    GREEN_DARK,
    GREEN_BRIGHT,
    COPPER
};
} // namespace

// ============================================================================
// Main Drawing Function
// ============================================================================

void draw_nozzle_a4t(lv_layer_t* layer, int32_t cx, int32_t cy, std::optional<lv_color_t> filament,
                     int32_t scale_unit, lv_opa_t opa) {
    int32_t render_size = scale_unit * 10;
    // Design space is 1000x1334; scale down to match the visual footprint
    // of the other renderers (bambu/stealthburner use 1000-unit space)
    float scale = (float)render_size / 2000.0f;

    const lv_color_t palette[] = {
        helix::nr_dim(lv_color_hex(0x1A1A1A), opa), // SILHOUETTE
        helix::nr_dim(lv_color_hex(0x353435), opa), // BODY
        helix::nr_dim(lv_color_hex(0x5F5E5F), opa), // DETAIL
        helix::nr_dim(lv_color_hex(0x0A0A0A), opa), // DARK
        helix::nr_dim(lv_color_hex(0xC7C8C5), opa), // HIGHLIGHT
        helix::nr_dim(lv_color_hex(0x282615), opa), // GREEN_SHADOW
        helix::nr_dim(lv_color_hex(0x615B12), opa), // GREEN_DARK
        helix::nr_dim(lv_color_hex(0xBFBB4B), opa), // GREEN_BRIGHT
        helix::nr_dim(lv_color_hex(0xA6614C), opa), // COPPER
    };

    static const helix::NrPolygon polys[] = {
        // Layer 1: Silhouette (dark outline under everything)
        helix::nr_poly(pts_silhouette, SILHOUETTE),
        helix::nr_poly(pts_silhouette_nozzle, SILHOUETTE),

        // Layer 2: Body (main gray surface)
        helix::nr_poly(pts_body_0, BODY),
        helix::nr_poly(pts_body_1, BODY),
        helix::nr_poly(pts_body_2, BODY),

        // Layer 3: Surface detail (mid-gray)
        helix::nr_poly(pts_detail_0, DETAIL),
        helix::nr_poly(pts_detail_1, DETAIL),
        helix::nr_poly(pts_detail_2, DETAIL),

        // Layer 4: Dark features (fan opening, mechanism, screw holes)
        helix::nr_poly(pts_fan_opening, DARK),
        helix::nr_poly(pts_dark_mechanism, DARK),
        helix::nr_poly(pts_screw_left, DARK),
        helix::nr_poly(pts_screw_right, DARK),

        // Layer 5: Highlights (PTFE tube, cube logo)
        helix::nr_poly(pts_highlight_top_left, HIGHLIGHT),
        helix::nr_poly(pts_highlight_top_right, HIGHLIGHT),
        helix::nr_poly(pts_highlight_cube_l, HIGHLIGHT),
        helix::nr_poly(pts_highlight_cube_r, HIGHLIGHT),
        helix::nr_poly(pts_highlight_cube_top, HIGHLIGHT),

        // Layer 6: Green shadow
        helix::nr_poly(pts_green_shadow_0, GREEN_SHADOW),
        helix::nr_poly(pts_green_shadow_1, GREEN_SHADOW),
        helix::nr_poly(pts_green_shadow_band, GREEN_SHADOW),

        // Layer 7: Green dark
        helix::nr_poly(pts_green_dark_0, GREEN_DARK),
        helix::nr_poly(pts_green_dark_1, GREEN_DARK),
        helix::nr_poly(pts_green_dark_band, GREEN_DARK),

        // Layer 8: Bright green (A4T signature)
        helix::nr_poly(pts_green_bright_band, GREEN_BRIGHT),
        helix::nr_poly(pts_green_bright_top, GREEN_BRIGHT),
        helix::nr_poly(pts_green_bright_ptfe, GREEN_BRIGHT),

        // Layer 9: Copper nozzle
        helix::nr_poly(pts_copper, COPPER),
    };

    helix::nr_draw_polygons(layer, polys, std::size(polys), palette, cx, cy, scale,
                            {DESIGN_CENTER_X, DESIGN_CENTER_Y});

    // Layer 10: Nozzle tip (shows filament color when loaded)
    {
        int32_t nozzle_top_y = cy + (int32_t)((1300 - DESIGN_CENTER_Y) * scale);
        int32_t nozzle_height = LV_MAX((int32_t)(30 * scale), 2);
        int32_t nozzle_top_width = LV_MAX((int32_t)(80 * scale), 4);
        int32_t nozzle_bottom_width = LV_MAX((int32_t)(30 * scale), 2);

        helix::nr_draw_filament_tip(layer, cx, nozzle_top_y, nozzle_top_width, nozzle_bottom_width,
                                    nozzle_height, filament, opa);
    }
}
