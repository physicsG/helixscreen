// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/// @file nozzle_renderer_jabberwocky.cpp
/// @brief JabberWocky V80 toolhead renderer implementation
///
/// Traced from JabberWocky V80 SVG — polygon-based rendering using LVGL triangle
/// primitives with ear-clipping triangulation, matching the approach
/// of nozzle_renderer_a4t.cpp and nozzle_renderer_stealthburner.cpp.

#include "nozzle_renderer_jabberwocky.h"

#include "nozzle_renderer_common.h"

#include <cmath>
#include <iterator>

// ============================================================================
// Polygon Data (design space: width=1000, height=1542)
// Traced from JabberWocky V80 SVG (1420x1012)
// Visual center measured from bounding box: X=500, Y=687
// ============================================================================

static constexpr int DESIGN_CENTER_X = 590;
static constexpr int DESIGN_CENTER_Y = 687;

// --- body_dark: dark grays forming base/silhouette (6 polygons) ---

static const lv_point_t pts_body_dark_0[] = {
    {360, 231}, {364, 310}, {331, 350}, {311, 363}, {306, 347}, {292, 344},
    {305, 327}, {289, 318}, {327, 282}, {348, 227}, {353, 224},
};

static const lv_point_t pts_body_dark_1[] = {
    {931, 173}, {931, 231}, {908, 232}, {917, 239}, {908, 248}, {910, 273}, {921, 258}, {918, 255},
    {927, 253}, {923, 321}, {911, 324}, {866, 319}, {861, 315}, {861, 292}, {871, 171}, {896, 167},
};

static const lv_point_t pts_body_dark_2[] = {
    {869, 502}, {885, 502}, {892, 519}, {885, 537}, {869, 537},
};

static const lv_point_t pts_body_dark_3[] = {
    {518, 927},  {488, 949},  {466, 977},  {472, 986},  {502, 1000}, {490, 1021},
    {450, 1018}, {448, 1031}, {442, 1028}, {440, 1084}, {447, 1092}, {440, 1095},
    {437, 1033}, {446, 996},  {479, 948},  {513, 921},
};

static const lv_point_t pts_body_dark_4[] = {
    {705, 960},  {711, 960},  {718, 968},  {738, 1004}, {745, 1045}, {745, 1056},
    {738, 1059}, {720, 1061}, {711, 1056}, {698, 1034}, {727, 1032}, {729, 1015},
    {692, 1021}, {689, 1018}, {695, 1011}, {724, 998},  {718, 977},
};

static const lv_point_t pts_body_dark_5[] = {
    {647, 1150}, {667, 1156}, {679, 1153}, {692, 1165}, {681, 1171}, {679, 1185}, {644, 1205},
    {640, 1202}, {663, 1190}, {655, 1173}, {652, 1176}, {642, 1163}, {641, 1155},
};

// --- body_mid: main body panels (6 polygons) ---

static const lv_point_t pts_body_mid_0[] = {
    {437, 176}, {435, 197}, {425, 212}, {398, 221}, {384, 215}, {383, 206}, {406, 202}, {432, 176},
};

static const lv_point_t pts_body_mid_1[] = {
    {353, 202}, {360, 231}, {353, 224}, {348, 227}, {327, 282}, {313, 291}, {305, 305}, {289, 302},
    {289, 292}, {302, 287}, {292, 273}, {308, 269}, {300, 263}, {298, 253}, {318, 252}, {310, 244},
    {308, 234}, {326, 237}, {318, 223}, {323, 215}, {340, 219}, {334, 202}, {352, 208},
};

static const lv_point_t pts_body_mid_2[] = {
    {931, 231}, {927, 276}, {927, 253}, {918, 255}, {921, 258},
    {910, 273}, {908, 248}, {917, 239}, {908, 232},
};

static const lv_point_t pts_body_mid_3[] = {
    {853, 482}, {858, 520}, {874, 556}, {811, 544}, {756, 431}, {750, 399},
    {732, 368}, {727, 344}, {734, 282}, {750, 266}, {755, 282}, {758, 279},
    {766, 284}, {779, 300}, {776, 303}, {781, 324}, {810, 431}, {845, 495},
};

static const lv_point_t pts_body_mid_4[] = {
    {747, 98},  {792, 137}, {824, 185}, {819, 188}, {794, 166}, {779, 175}, {777, 184},
    {794, 195}, {793, 207}, {789, 195}, {779, 195}, {776, 219}, {771, 198}, {760, 195},
    {763, 215}, {760, 220}, {755, 218}, {754, 228}, {763, 247}, {753, 253}, {758, 273},
    {765, 253}, {767, 258}, {771, 256}, {774, 282}, {782, 265}, {790, 285}, {800, 260},
    {808, 263}, {809, 279}, {824, 279}, {823, 306}, {834, 331}, {840, 368}, {839, 408},
    {852, 456}, {856, 449}, {856, 473}, {866, 473}, {856, 478}, {845, 495}, {810, 431},
    {781, 324}, {778, 297}, {764, 282}, {758, 279}, {755, 282}, {750, 266}, {734, 282},
};

static const lv_point_t pts_body_mid_5[] = {
    {711, 960}, {705, 960}, {665, 928}, {618, 915}, {608, 903}, {633, 907}, {666, 920}, {686, 933},
};

// --- body_light: surface detail (14 polygons) ---

static const lv_point_t pts_body_light_0[] = {
    {553, 60}, {556, 69}, {658, 69}, {660, 85}, {521, 85}, {521, 66}, {538, 66},
};

static const lv_point_t pts_body_light_1[] = {
    {747, 98},  {811, 136}, {865, 189}, {869, 179}, {866, 227}, {861, 205}, {850, 202}, {845, 224},
    {842, 202}, {837, 198}, {831, 224}, {824, 202}, {815, 198}, {811, 224}, {806, 198}, {802, 195},
    {792, 240}, {808, 242}, {818, 255}, {808, 263}, {800, 260}, {790, 285}, {782, 265}, {774, 282},
    {771, 256}, {767, 258}, {765, 253}, {758, 273}, {753, 253}, {763, 247}, {754, 224}, {755, 218},
    {760, 220}, {763, 215}, {760, 195}, {771, 198}, {776, 219}, {779, 195}, {789, 195}, {794, 206},
    {794, 195}, {777, 184}, {782, 172}, {794, 166}, {819, 188}, {824, 185}, {792, 137},
};

static const lv_point_t pts_body_light_2[] = {
    {731, 205}, {726, 373}, {811, 544}, {874, 556}, {881, 551}, {889, 553}, {887, 563}, {913, 589},
    {911, 598}, {927, 656}, {892, 656}, {884, 598}, {863, 568}, {826, 553}, {782, 577}, {771, 578},
    {749, 568}, {729, 547}, {726, 560}, {727, 542}, {716, 527}, {704, 565}, {698, 603}, {702, 631},
    {698, 660}, {708, 673}, {705, 685}, {673, 685}, {652, 366}, {642, 353}, {627, 345}, {553, 344},
    {518, 348}, {504, 360}, {497, 373}, {473, 685}, {452, 685}, {450, 680}, {465, 602}, {487, 373},
    {495, 354}, {512, 337}, {531, 329}, {621, 329}, {650, 345}, {668, 373}, {664, 277}, {653, 182},
    {667, 174}, {674, 160}, {686, 180}, {685, 203}, {690, 211}, {692, 169}, {708, 177}, {721, 219},
    {724, 221},
};

static const lv_point_t pts_body_light_3[] = {
    {931, 173},
    {937, 179},
    {931, 231},
};

static const lv_point_t pts_body_light_4[] = {
    {437, 169}, {436, 178}, {432, 176}, {410, 200}, {383, 207}, {384, 215},
    {398, 221}, {390, 356}, {331, 376}, {366, 304}, {359, 225}, {350, 195},
    {363, 160}, {385, 132}, {419, 121}, {427, 123}, {418, 132}, {416, 144},
};

static const lv_point_t pts_body_light_5[] = {
    {863, 269}, {861, 315}, {866, 319}, {908, 324}, {908, 328}, {902, 346}, {890, 356},
    {879, 406}, {881, 411}, {902, 398}, {905, 402}, {875, 425}, {869, 432}, {873, 437},
    {856, 460}, {855, 448}, {852, 456}, {839, 408}, {840, 368}, {834, 331}, {823, 306},
    {824, 279}, {811, 279}, {816, 263}, {821, 266}, {826, 263}, {829, 282}, {833, 277},
    {835, 285}, {840, 282}, {845, 285}, {852, 260}, {858, 279},
};

static const lv_point_t pts_body_light_6[] = {
    {927, 289}, {931, 313}, {926, 389}, {914, 399}, {905, 402}, {902, 398},
    {881, 411}, {890, 356}, {904, 343}, {908, 324}, {923, 321},
};

static const lv_point_t pts_body_light_7[] = {
    {292, 344}, {308, 350}, {292, 348}, {255, 382}, {253, 392}, {244, 392}, {253, 377},
    {251, 366}, {255, 357}, {260, 353}, {263, 356}, {277, 337}, {283, 345},
};

static const lv_point_t pts_body_light_8[] = {
    {237, 395},
    {231, 405},
    {195, 431},
    {212, 408},
};

static const lv_point_t pts_body_light_9[] = {
    {892, 656}, {715, 660}, {708, 673}, {705, 671}, {698, 660}, {702, 631},
    {698, 603}, {704, 565}, {716, 527}, {727, 542}, {726, 560}, {729, 547},
    {749, 568}, {771, 578}, {782, 577}, {826, 553}, {863, 568}, {884, 598},
};

static const lv_point_t pts_body_light_10[] = {
    {166, 689}, {192, 695}, {198, 692}, {195, 727}, {171, 737}, {158, 727}, {157, 719}, {158, 703},
};

static const lv_point_t pts_body_light_11[] = {
    {405, 792}, {438, 794}, {451, 809}, {455, 834}, {447, 848},
    {433, 860}, {406, 858}, {398, 855}, {390, 841}, {387, 815},
};

static const lv_point_t pts_body_light_12[] = {
    {744, 792}, {776, 795}, {792, 810}, {793, 827}, {787, 850},
    {772, 859}, {747, 860}, {726, 837}, {726, 815},
};

static const lv_point_t pts_body_light_13[] = {
    {321, 805}, {340, 808}, {308, 810}, {305, 811}, {318, 818}, {302, 818}, {302, 805},
};

// --- body_highlight: bright detail panels (11 polygons) ---

static const lv_point_t pts_body_highlight_0[] = {
    {534, 47},
    {534, 56},
    {553, 60},
    {531, 60},
};

static const lv_point_t pts_body_highlight_1[] = {
    {660, 85},  {698, 93},  {714, 106}, {728, 127}, {731, 124}, {733, 180}, {724, 221}, {712, 185},
    {701, 171}, {692, 169}, {690, 211}, {685, 203}, {686, 180}, {674, 160}, {667, 174}, {653, 182},
    {650, 174}, {669, 152}, {661, 144}, {640, 174}, {631, 163}, {619, 160}, {611, 161}, {618, 165},
    {437, 169}, {416, 144}, {418, 132}, {427, 123}, {419, 121}, {385, 132}, {376, 140}, {363, 160},
    {356, 182}, {359, 151}, {369, 123}, {366, 121}, {389, 98},  {390, 102}, {424, 90},
};

static const lv_point_t pts_body_highlight_2[] = {
    {653, 182}, {664, 277}, {668, 373}, {650, 345}, {621, 329}, {531, 329}, {502, 344}, {487, 373},
    {465, 602}, {450, 679}, {437, 660}, {398, 656}, {413, 640}, {417, 646}, {424, 642}, {432, 623},
    {430, 597}, {417, 575}, {384, 547}, {359, 556}, {339, 573}, {346, 556}, {356, 547}, {392, 540},
    {400, 366}, {384, 366}, {368, 373}, {363, 369}, {322, 386}, {279, 395}, {279, 411}, {252, 410},
    {232, 416}, {231, 424}, {244, 431}, {276, 431}, {273, 445}, {253, 448}, {235, 456}, {226, 447},
    {221, 459}, {212, 462}, {205, 461}, {218, 460}, {218, 447}, {206, 453}, {198, 447}, {201, 442},
    {231, 408}, {390, 356}, {398, 221}, {414, 218}, {430, 208}, {437, 191}, {437, 169}, {618, 165},
    {611, 161}, {619, 160}, {631, 163}, {640, 174}, {661, 144}, {669, 152}, {650, 174},
};

static const lv_point_t pts_body_highlight_3[] = {
    {866, 227}, {863, 269}, {858, 279}, {852, 260}, {845, 285}, {840, 282}, {835, 285}, {833, 277},
    {829, 282}, {826, 263}, {821, 266}, {816, 263}, {809, 278}, {808, 263}, {818, 255}, {816, 251},
    {805, 241}, {792, 240}, {802, 195}, {806, 198}, {811, 224}, {815, 198}, {824, 202}, {831, 224},
    {837, 198}, {842, 202}, {845, 224}, {850, 202}, {861, 205},
};

static const lv_point_t pts_body_highlight_4[] = {
    {398, 656}, {327, 656}, {316, 615}, {206, 496}, {195, 502}, {186, 471}, {192, 440},
    {197, 482}, {198, 447}, {206, 453}, {218, 447}, {218, 460}, {205, 461}, {212, 462},
    {221, 459}, {226, 447}, {235, 456}, {273, 444}, {276, 431}, {244, 431}, {231, 424},
    {234, 415}, {256, 409}, {279, 411}, {279, 395}, {322, 386}, {363, 369}, {368, 373},
    {384, 366}, {400, 366}, {392, 540}, {356, 547}, {346, 556}, {339, 573}, {359, 556},
    {384, 547}, {417, 575}, {430, 597}, {432, 623}, {424, 642}, {417, 646}, {413, 640},
};

static const lv_point_t pts_body_highlight_5[] = {
    {531, 544}, {515, 550}, {511, 524}, {515, 498}, {521, 495},
    {524, 527}, {556, 511}, {561, 453}, {569, 447}, {571, 518},
};

static const lv_point_t pts_body_highlight_6[] = {
    {611, 521}, {624, 527}, {627, 495}, {634, 498}, {637, 524}, {634, 550},
    {579, 521}, {576, 484}, {579, 447}, {587, 453}, {592, 511},
};

static const lv_point_t pts_body_highlight_7[] = {
    {537, 521},
    {529, 524},
    {529, 495},
    {540, 498},
};

static const lv_point_t pts_body_highlight_8[] = {
    {611, 521},
    {609, 506},
    {618, 495},
    {619, 524},
};

static const lv_point_t pts_body_highlight_9[] = {
    {579, 521},
    {634, 556},
    {574, 592},
    {515, 555},
};

static const lv_point_t pts_body_highlight_10[] = {
    {544, 1505}, {634, 1505}, {613, 1514}, {598, 1536}, {589, 1540}, {581, 1537}, {563, 1510},
};

// --- dark_features: holes, cutouts (12 polygons) ---

static const lv_point_t pts_dark_0[] = {
    {550, 905}, {577, 950}, {579, 961}, {574, 969}, {566, 968}, {518, 919},
};

static const lv_point_t pts_dark_1[] = {
    {705, 960}, {718, 977}, {724, 998}, {689, 1015}, {648, 979}, {636, 945}, {618, 915}, {665, 928},
};

static const lv_point_t pts_dark_2[] = {
    {566, 902},
    {595, 905},
    {623, 956},
    {623, 979},
};

static const lv_point_t pts_dark_3[] = {
    {518, 927},  {550, 973},  {552, 987},  {544, 997},  {502, 1010}, {524, 1019},
    {488, 1049}, {447, 1071}, {473, 1079}, {445, 1115}, {440, 1099}, {447, 1092},
    {440, 1084}, {442, 1029}, {445, 1026}, {448, 1031}, {450, 1018}, {490, 1021},
    {502, 1000}, {472, 986},  {466, 977},  {488, 949},
};

static const lv_point_t pts_dark_4[] = {
    {563, 1005}, {569, 1008}, {561, 1027}, {506, 1124}, {507, 1160}, {518, 1194},
    {492, 1179}, {475, 1161}, {468, 1139}, {471, 1114}, {484, 1085}, {492, 1078},
    {492, 1068}, {537, 1035}, {547, 1026}, {537, 1021},
};

static const lv_point_t pts_dark_5[] = {
    {698, 1034},
    {692, 1021},
    {729, 1015},
    {727, 1032},
};

static const lv_point_t pts_dark_6[] = {
    {608, 982},  {663, 1010}, {681, 1027}, {677, 1034}, {644, 1023}, {656, 1042},
    {682, 1045}, {708, 1071}, {734, 1076}, {722, 1082}, {710, 1076}, {711, 1111},
    {698, 1108}, {676, 1077}, {642, 1060}, {641, 1068}, {660, 1095}, {626, 1079},
    {597, 1053}, {587, 1028}, {587, 1002}, {597, 998},  {606, 1002},
};

static const lv_point_t pts_dark_7[] = {
    {576, 1056}, {601, 1080}, {601, 1085}, {589, 1098}, {537, 1102}, {539, 1092},
};

static const lv_point_t pts_dark_8[] = {
    {663, 1150}, {652, 1146}, {641, 1158}, {652, 1176}, {655, 1173}, {663, 1190}, {640, 1202},
    {615, 1181}, {605, 1157}, {602, 1129}, {603, 1108}, {616, 1089}, {656, 1116}, {664, 1134},
};

static const lv_point_t pts_dark_9[] = {
    {682, 1182}, {681, 1171}, {692, 1165}, {679, 1153}, {693, 1143}, {694, 1132},
    {679, 1118}, {660, 1081}, {680, 1093}, {685, 1118}, {700, 1119}, {708, 1131},
    {720, 1117}, {721, 1098}, {740, 1102}, {719, 1148},
};

static const lv_point_t pts_dark_10[] = {
    {531, 1105},
    {576, 1113},
    {582, 1144},
    {527, 1144},
};

static const lv_point_t pts_dark_11[] = {
    {534, 1150}, {579, 1156}, {582, 1179}, {544, 1181}, {585, 1190},
    {608, 1213}, {583, 1214}, {561, 1205}, {535, 1174}, {527, 1155},
};

// --- copper_dark (7 polygons) ---

static const lv_point_t pts_copper_dark_0[] = {
    {360, 318},
    {331, 376},
    {321, 379},
    {333, 346},
};

static const lv_point_t pts_copper_dark_1[] = {
    {198, 692}, {192, 695}, {166, 689}, {171, 676}, {188, 671}, {195, 678},
};

static const lv_point_t pts_copper_dark_2[] = {
    {244, 656},
    {234, 724},
    {224, 724},
};

static const lv_point_t pts_copper_dark_3[] = {
    {505, 1411}, {547, 1411}, {529, 1463}, {508, 1485}, {495, 1427}, {505, 1427},
};

static const lv_point_t pts_copper_dark_4[] = {
    {547, 1411}, {637, 1411}, {648, 1453}, {666, 1471}, {663, 1474}, {669, 1482}, {656, 1477},
    {627, 1444}, {579, 1434}, {547, 1439}, {534, 1469}, {511, 1482}, {529, 1463},
};

static const lv_point_t pts_copper_dark_5[] = {
    {637, 1411}, {676, 1411}, {685, 1435}, {673, 1482}, {648, 1453},
};

static const lv_point_t pts_copper_dark_6[] = {
    {440, 1469}, {447, 1482}, {440, 1518}, {421, 1518},
    {423, 1511}, {429, 1511}, {437, 1504}, {427, 1497},
};

// --- copper_mid (14 polygons) ---

static const lv_point_t pts_copper_mid_0[] = {
    {521, 85}, {660, 85}, {424, 90}, {389, 99}, {415, 87},
};

static const lv_point_t pts_copper_mid_1[] = {
    {689, 89}, {711, 95}, {726, 111}, {730, 127}, {711, 103},
};

static const lv_point_t pts_copper_mid_2[] = {
    {366, 121}, {369, 123}, {359, 151}, {353, 189}, {353, 153},
};

static const lv_point_t pts_copper_mid_3[] = {
    {124, 656},
    {95, 724},
    {65, 721},
    {65, 660},
};

static const lv_point_t pts_copper_mid_4[] = {
    {405, 773}, {449, 779}, {464, 792}, {468, 801}, {466, 824}, {453, 853},
    {435, 864}, {405, 865}, {387, 850}, {381, 838}, {384, 785},
};

static const lv_point_t pts_copper_mid_5[] = {
    {750, 769}, {784, 775}, {801, 787}, {811, 801}, {810, 812}, {802, 823}, {794, 848},
    {785, 859}, {771, 865}, {744, 865}, {727, 851}, {719, 834}, {724, 806}, {719, 789},
};

static const lv_point_t pts_copper_mid_6[] = {
    {318, 1047},
    {318, 1056},
    {331, 1060},
    {315, 1060},
};

static const lv_point_t pts_copper_mid_7[] = {
    {360, 1402}, {824, 1402}, {840, 1413}, {381, 1412},
    {355, 1405}, {347, 1415}, {351, 1401}, {355, 1398},
};

static const lv_point_t pts_copper_mid_8[] = {
    {256, 1408}, {365, 1444}, {373, 1440}, {379, 1427}, {395, 1515},
    {353, 1515}, {256, 1484}, {243, 1421}, {247, 1413},
};

static const lv_point_t pts_copper_mid_9[] = {
    {389, 1411}, {405, 1411}, {440, 1469}, {427, 1497}, {437, 1504}, {429, 1511},
    {423, 1511}, {421, 1518}, {353, 1515}, {395, 1515}, {379, 1427},
};

static const lv_point_t pts_copper_mid_10[] = {
    {776, 1411}, {792, 1411}, {815, 1444}, {894, 1418}, {905, 1423}, {934, 1440},
    {924, 1484}, {827, 1515}, {769, 1515}, {789, 1453}, {786, 1433},
};

static const lv_point_t pts_copper_mid_11[] = {
    {776, 1418}, {785, 1431}, {789, 1453}, {769, 1515}, {827, 1515}, {740, 1518}, {735, 1476},
};

static const lv_point_t pts_copper_mid_12[] = {
    {669, 1482}, {671, 1487}, {663, 1503}, {643, 1505}, {634, 1505}, {656, 1500},
    {637, 1485}, {640, 1479}, {637, 1473}, {620, 1470}, {562, 1467}, {539, 1473},
    {531, 1477}, {544, 1484}, {518, 1500}, {547, 1505}, {515, 1503}, {508, 1485},
    {534, 1469}, {547, 1439}, {579, 1434}, {627, 1444}, {656, 1477},
};

static const lv_point_t pts_copper_mid_13[] = {
    {634, 1505}, {547, 1505}, {518, 1500}, {544, 1484}, {531, 1477}, {539, 1473},
    {553, 1468}, {587, 1467}, {637, 1473}, {640, 1479}, {637, 1485}, {656, 1500},
};

// --- copper_bright: bright orange nozzle highlights (19 polygons) ---

static const lv_point_t pts_copper_bright_0[] = {
    {244, 392},
    {273, 392},
    {231, 408},
    {238, 394},
};

static const lv_point_t pts_copper_bright_1[] = {
    {224, 518}, {316, 615}, {327, 656}, {265, 658}, {261, 660},
    {260, 685}, {240, 685}, {247, 629}, {246, 593}, {237, 550},
};

static const lv_point_t pts_copper_bright_2[] = {
    {282, 656},  {289, 721}, {263, 1340}, {247, 1341}, {221, 1334},
    {211, 1283}, {234, 724}, {240, 685},  {260, 685},  {261, 660},
};

static const lv_point_t pts_copper_bright_3[] = {
    {282, 656}, {410, 656}, {437, 660}, {453, 685}, {437, 713}, {629, 715}, {711, 715}, {702, 692},
    {715, 660}, {927, 656}, {940, 682}, {863, 685}, {873, 976}, {842, 982}, {785, 976}, {766, 979},
    {745, 944}, {705, 904}, {673, 885}, {615, 868}, {559, 870}, {520, 881}, {482, 898}, {466, 897},
    {442, 918}, {427, 945}, {348, 944}, {315, 940}, {298, 931}, {279, 934}, {289, 721},
};

static const lv_point_t pts_copper_bright_4[] = {
    {453, 685}, {705, 685}, {702, 692}, {711, 715}, {437, 713},
};

static const lv_point_t pts_copper_bright_5[] = {
    {940, 682}, {947, 737}, {968, 1305}, {960, 1334}, {925, 1340}, {889, 1340}, {863, 685},
};

static const lv_point_t pts_copper_bright_6[] = {
    {750, 753}, {783, 757}, {800, 765}, {820, 787}, {829, 808}, {829, 842}, {822, 861}, {800, 883},
    {779, 894}, {756, 894}, {727, 885}, {711, 872}, {694, 844}, {693, 818}, {703, 786}, {717, 770},
};

static const lv_point_t pts_copper_bright_7[] = {
    {311, 892}, {323, 893}, {330, 899}, {329, 915}, {321, 920}, {314, 920}, {306, 911}, {306, 901},
};

static const lv_point_t pts_copper_bright_8[] = {
    {482, 898},  {442, 934},  {418, 977},  {401, 1039}, {400, 1089}, {422, 1153}, {447, 1187},
    {497, 1225}, {540, 1242}, {605, 1246}, {646, 1239}, {683, 1224}, {719, 1200}, {748, 1166},
    {774, 1107}, {780, 1045}, {766, 979},  {785, 976},  {842, 982},  {873, 976},  {889, 1340},
    {776, 1344}, {789, 1379}, {811, 1383}, {824, 1402}, {360, 1402}, {372, 1385}, {392, 1379},
    {405, 1344}, {263, 1340}, {279, 934},  {295, 934},  {297, 944},  {321, 963},  {312, 966},
    {297, 958},  {297, 969},  {305, 974},  {297, 979},  {297, 992},  {310, 983},  {324, 987},
    {294, 1005}, {295, 1018}, {334, 989},  {327, 976},  {333, 967},  {332, 956},  {315, 940},
    {427, 945},  {442, 918},  {466, 897},
};

static const lv_point_t pts_copper_bright_9[] = {
    {663, 1150},
    {657, 1154},
    {647, 1150},
    {656, 1145},
};

static const lv_point_t pts_copper_bright_10[] = {
    {302, 1031},
    {308, 1042},
    {305, 1056},
};

static const lv_point_t pts_copper_bright_11[] = {
    {308, 1140},
    {318, 1140},
    {318, 1169},
    {308, 1169},
};

static const lv_point_t pts_copper_bright_12[] = {
    {295, 1144},
    {301, 1155},
    {298, 1169},
};

static const lv_point_t pts_copper_bright_13[] = {
    {305, 1198},
    {315, 1198},
    {315, 1227},
    {305, 1227},
};

static const lv_point_t pts_copper_bright_14[] = {
    {292, 1202},
    {298, 1211},
    {295, 1224},
};

static const lv_point_t pts_copper_bright_15[] = {
    {302, 1266},
    {305, 1272},
    {302, 1279},
    {295, 1273},
};

static const lv_point_t pts_copper_bright_16[] = {
    {221, 1334}, {260, 1340}, {256, 1408}, {245, 1414}, {244, 1427},
};

static const lv_point_t pts_copper_bright_17[] = {
    {260, 1340}, {405, 1344}, {392, 1379}, {372, 1385}, {360, 1402}, {355, 1398}, {347, 1408},
    {347, 1415}, {355, 1405}, {371, 1411}, {389, 1411}, {373, 1440}, {365, 1444}, {256, 1408},
};

static const lv_point_t pts_copper_bright_18[] = {
    {960, 1334}, {934, 1444}, {931, 1438}, {894, 1418}, {815, 1444}, {792, 1411},
    {840, 1413}, {824, 1402}, {811, 1383}, {789, 1379}, {776, 1344}, {918, 1341},
};

// --- blue_dark (3 polygons) ---

static const lv_point_t pts_blue_dark_0[] = {
    {647, 47}, {647, 56}, {624, 56}, {624, 69}, {556, 69},
    {553, 60}, {534, 56}, {534, 47}, {545, 46},
};

static const lv_point_t pts_blue_dark_1[] = {
    {247, 602}, {244, 656}, {218, 747}, {213, 763}, {195, 773}, {185, 768}, {197, 744}, {200, 750},
    {201, 736}, {195, 727}, {198, 708}, {208, 706}, {218, 694}, {218, 668}, {219, 663}, {222, 668},
    {224, 665}, {223, 650}, {226, 656}, {227, 652}, {224, 644}, {229, 644}, {234, 621},
};

static const lv_point_t pts_blue_dark_2[] = {
    {953, 715}, {961, 834}, {972, 858}, {990, 876}, {993, 893},
    {988, 911}, {971, 918}, {953, 918}, {946, 732},
};

// --- blue_bright: JabberWocky signature color (21 polygons) ---

static const lv_point_t pts_blue_0[] = {
    {747, 34}, {740, 60}, {676, 56}, {702, 15}, {715, 5}, {747, 8},
};

static const lv_point_t pts_blue_1[] = {
    {747, 34}, {747, 98}, {727, 331}, {734, 164}, {730, 121}, {719, 98},
    {683, 87}, {660, 85}, {660, 73},  {676, 56},  {740, 60},
};

static const lv_point_t pts_blue_2[] = {
    {308, 350}, {318, 379}, {287, 390}, {253, 392}, {255, 382}, {292, 348},
};

static const lv_point_t pts_blue_3[] = {
    {727, 337}, {730, 362}, {748, 395}, {756, 431}, {726, 373},
};

static const lv_point_t pts_blue_4[] = {
    {208, 502}, {223, 516}, {231, 534}, {247, 602}, {234, 621}, {229, 644}, {225, 643},
    {224, 665}, {222, 668}, {219, 663}, {218, 668}, {219, 690}, {208, 706}, {197, 706},
    {197, 682}, {184, 671}, {171, 676}, {158, 703}, {158, 727}, {165, 734}, {171, 737},
    {195, 727}, {200, 732}, {200, 750}, {197, 744}, {185, 768}, {195, 773}, {111, 816},
    {102, 834}, {98, 818},  {119, 789}, {132, 719}, {157, 629},
};

static const lv_point_t pts_blue_5[] = {
    {915, 595}, {927, 618}, {947, 687}, {953, 715}, {947, 724},
    {947, 737}, {940, 682}, {927, 656}, {911, 598},
};

static const lv_point_t pts_blue_6[] = {
    {208, 502}, {157, 629}, {132, 719}, {119, 789}, {98, 818}, {102, 834}, {86, 896},
    {65, 938},  {37, 977},  {20, 973},  {11, 966},  {-2, 934}, {195, 502}, {205, 495},
};

static const lv_point_t pts_blue_7[] = {
    {308, 705}, {342, 740}, {342, 756}, {329, 744}, {306, 760}, {321, 734}, {310, 721},
};

static const lv_point_t pts_blue_8[] = {
    {324, 805}, {306, 782}, {306, 766}, {324, 789}, {342, 769}, {342, 785},
};

static const lv_point_t pts_blue_9[] = {
    {340, 808},
    {340, 818},
    {318, 818},
    {305, 811},
};

static const lv_point_t pts_blue_10[] = {
    {305, 821}, {315, 826}, {309, 849}, {313, 860}, {320, 862}, {329, 856}, {331, 824},
    {340, 824}, {339, 860}, {326, 872}, {312, 872}, {306, 866}, {302, 846},
};

static const lv_point_t pts_blue_11[] = {
    {766, 979},  {777, 1023}, {780, 1066}, {767, 1127}, {735, 1185}, {701, 1213}, {665, 1233},
    {626, 1244}, {584, 1247}, {540, 1242}, {518, 1234}, {462, 1201}, {434, 1171}, {413, 1133},
    {399, 1072}, {401, 1039}, {412, 992},  {442, 934},  {474, 905},  {507, 886},  {546, 873},
    {587, 867},  {615, 868},  {654, 877},  {690, 893},  {720, 916},  {745, 944},
};

static const lv_point_t pts_blue_12[] = {
    {663, 1150},
    {657, 1154},
    {647, 1150},
    {656, 1145},
};

static const lv_point_t pts_blue_13[] = {
    {308, 879}, {325, 880}, {335, 892}, {336, 913}, {327, 932}, {311, 933}, {300, 921}, {299, 899},
};

static const lv_point_t pts_blue_14[] = {
    {315, 940}, {332, 956}, {333, 967}, {327, 976}, {334, 989}, {295, 1018}, {294, 1005},
    {324, 987}, {312, 983}, {297, 992}, {295, 988}, {297, 979}, {305, 974},  {297, 969},
    {297, 958}, {312, 966}, {321, 963}, {297, 944}, {295, 933}, {298, 931},
};

static const lv_point_t pts_blue_15[] = {
    {318, 1047}, {315, 1060}, {331, 1060}, {331, 1069}, {292, 1069},
    {294, 1027}, {305, 1021}, {319, 1031}, {328, 1026}, {329, 1037},
};

static const lv_point_t pts_blue_16[] = {
    {289, 1076}, {298, 1076}, {302, 1111}, {308, 1076}, {312, 1111},
    {321, 1111}, {324, 1076}, {327, 1121}, {289, 1121},
};

static const lv_point_t pts_blue_17[] = {
    {308, 1127}, {323, 1131}, {324, 1179}, {285, 1179}, {288, 1138}, {295, 1131}, {303, 1134},
};

static const lv_point_t pts_blue_18[] = {
    {305, 1185}, {323, 1192}, {324, 1237}, {282, 1237}, {287, 1192},
};

static const lv_point_t pts_blue_19[] = {
    {318, 1244}, {321, 1252}, {316, 1260}, {315, 1302}, {282, 1273}, {288, 1262},
};

static const lv_point_t pts_blue_20[] = {
    {279, 1308}, {321, 1310}, {326, 1323}, {324, 1327}, {318, 1319}, {279, 1318},
};

// --- green accents (8 polygons) ---

static const lv_point_t pts_green_0[] = {
    {711, 95},
    {720, 99},
    {724, 108},
};

static const lv_point_t pts_green_1[] = {
    {289, 302}, {305, 305}, {289, 318}, {305, 327}, {290, 345},
    {283, 345}, {277, 337}, {261, 356}, {268, 324},
};

static const lv_point_t pts_green_2[] = {
    {331, 350},
    {324, 373},
    {318, 379},
    {311, 363},
};

static const lv_point_t pts_green_3[] = {
    {521, 347}, {637, 353}, {650, 421}, {645, 442}, {652, 466}, {660, 577},
    {656, 584}, {666, 665}, {663, 679}, {489, 679}, {497, 373}, {506, 357},
};

static const lv_point_t pts_green_4[] = {
    {521, 347}, {627, 345}, {642, 353}, {652, 366}, {673, 685}, {473, 685},
    {492, 473}, {495, 497}, {488, 588}, {489, 679}, {663, 679}, {666, 665},
    {656, 584}, {660, 577}, {652, 466}, {645, 442}, {650, 421}, {637, 353},
};

static const lv_point_t pts_green_5[] = {
    {905, 402}, {910, 421}, {911, 585}, {887, 563}, {889, 553}, {876, 553}, {867, 540},
    {856, 513}, {853, 490}, {855, 480}, {875, 467}, {881, 457}, {869, 432},
};

static const lv_point_t pts_green_6[] = {
    {231, 405},
    {198, 447},
    {197, 482},
    {193, 434},
};

static const lv_point_t pts_green_7[] = {
    {563, 540}, {585, 542}, {602, 556}, {583, 571}, {561, 570}, {547, 556},
};

// --- highlights (2 polygons) ---

static const lv_point_t pts_highlight_0[] = {
    {747, 66}, {747, 8}, {716, 5}, {705, 11}, {705, 2}, {740, 3}, {751, 10},
};

static const lv_point_t pts_highlight_1[] = {
    {647, 47}, {650, 60}, {627, 63}, {656, 69}, {624, 69}, {624, 56}, {647, 56},
};

namespace {
enum : uint8_t {
    BODY_DARK,
    BODY_MID,
    BODY_LIGHT,
    BODY_HIGHLIGHT,
    DARK,
    COPPER_DARK,
    COPPER_MID,
    COPPER_BRIGHT,
    BLUE_DARK,
    BLUE_BRIGHT,
    GREEN,
    HIGHLIGHT
};
} // namespace

// ============================================================================
// Main Drawing Function
// ============================================================================

void draw_nozzle_jabberwocky(lv_layer_t* layer, int32_t cx, int32_t cy,
                             std::optional<lv_color_t> filament, int32_t scale_unit, lv_opa_t opa) {
    int32_t render_size = scale_unit * 10;
    // Design space is 1000x1542; scale down to match the visual footprint
    // of the other renderers (bambu/stealthburner use 1000-unit space)
    float scale = (float)render_size / 2400.0f;

    const lv_color_t palette[] = {
        helix::nr_dim(lv_color_hex(0x2A2A28), opa), // BODY_DARK
        helix::nr_dim(lv_color_hex(0x4C4B47), opa), // BODY_MID
        helix::nr_dim(lv_color_hex(0x7A7972), opa), // BODY_LIGHT
        helix::nr_dim(lv_color_hex(0x969690), opa), // BODY_HIGHLIGHT
        helix::nr_dim(lv_color_hex(0x0A0A0A), opa), // DARK
        helix::nr_dim(lv_color_hex(0x7A4D20), opa), // COPPER_DARK
        helix::nr_dim(lv_color_hex(0xB06525), opa), // COPPER_MID
        helix::nr_dim(lv_color_hex(0xE87828), opa), // COPPER_BRIGHT
        helix::nr_dim(lv_color_hex(0x2D5070), opa), // BLUE_DARK
        helix::nr_dim(lv_color_hex(0x3890C0), opa), // BLUE_BRIGHT
        helix::nr_dim(lv_color_hex(0x50A050), opa), // GREEN
        helix::nr_dim(lv_color_hex(0xC0C0B8), opa), // HIGHLIGHT
    };

    static const helix::NrPolygon polys[] = {
        // === BASE STRUCTURE ===
        // Body dark (base silhouette)
        helix::nr_poly(pts_body_dark_0, BODY_DARK),
        helix::nr_poly(pts_body_dark_1, BODY_DARK),
        helix::nr_poly(pts_body_dark_2, BODY_DARK),
        helix::nr_poly(pts_body_dark_3, BODY_DARK),
        helix::nr_poly(pts_body_dark_4, BODY_DARK),
        helix::nr_poly(pts_body_dark_5, BODY_DARK),

        // Body mid (main panels)
        helix::nr_poly(pts_body_mid_0, BODY_MID),
        helix::nr_poly(pts_body_mid_1, BODY_MID),
        helix::nr_poly(pts_body_mid_2, BODY_MID),
        helix::nr_poly(pts_body_mid_3, BODY_MID),
        helix::nr_poly(pts_body_mid_4, BODY_MID),
        helix::nr_poly(pts_body_mid_5, BODY_MID),

        // Body light (surface detail, excluding screw highlights 11/12)
        helix::nr_poly(pts_body_light_0, BODY_LIGHT),
        helix::nr_poly(pts_body_light_1, BODY_LIGHT),
        helix::nr_poly(pts_body_light_2, BODY_LIGHT),
        helix::nr_poly(pts_body_light_3, BODY_LIGHT),
        helix::nr_poly(pts_body_light_4, BODY_LIGHT),
        helix::nr_poly(pts_body_light_5, BODY_LIGHT),
        helix::nr_poly(pts_body_light_6, BODY_LIGHT),
        helix::nr_poly(pts_body_light_7, BODY_LIGHT),
        helix::nr_poly(pts_body_light_8, BODY_LIGHT),
        helix::nr_poly(pts_body_light_9, BODY_LIGHT),
        helix::nr_poly(pts_body_light_10, BODY_LIGHT),
        helix::nr_poly(pts_body_light_13, BODY_LIGHT),

        // Body highlight (bright panels)
        helix::nr_poly(pts_body_highlight_0, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_1, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_2, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_3, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_4, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_5, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_6, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_7, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_8, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_9, BODY_HIGHLIGHT),
        helix::nr_poly(pts_body_highlight_10, BODY_HIGHLIGHT),

        // === ACCENTS + COPPER BODY ===
        // Green accents
        helix::nr_poly(pts_green_0, GREEN),
        helix::nr_poly(pts_green_1, GREEN),
        helix::nr_poly(pts_green_2, GREEN),
        helix::nr_poly(pts_green_3, GREEN),
        helix::nr_poly(pts_green_4, GREEN),
        helix::nr_poly(pts_green_5, GREEN),
        helix::nr_poly(pts_green_6, GREEN),
        helix::nr_poly(pts_green_7, GREEN),

        // Blue dark (shadow)
        helix::nr_poly(pts_blue_dark_0, BLUE_DARK),
        helix::nr_poly(pts_blue_dark_1, BLUE_DARK),
        helix::nr_poly(pts_blue_dark_2, BLUE_DARK),

        // Blue bright (non-screw, non-circle features)
        helix::nr_poly(pts_blue_0, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_1, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_2, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_3, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_4, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_5, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_6, BLUE_BRIGHT),

        // Copper dark (nozzle shadow)
        helix::nr_poly(pts_copper_dark_0, COPPER_DARK),
        helix::nr_poly(pts_copper_dark_1, COPPER_DARK),
        helix::nr_poly(pts_copper_dark_2, COPPER_DARK),
        helix::nr_poly(pts_copper_dark_3, COPPER_DARK),
        helix::nr_poly(pts_copper_dark_4, COPPER_DARK),
        helix::nr_poly(pts_copper_dark_5, COPPER_DARK),
        helix::nr_poly(pts_copper_dark_6, COPPER_DARK),

        // Copper mid (nozzle body, excluding screw copper 4/5)
        helix::nr_poly(pts_copper_mid_0, COPPER_MID),
        helix::nr_poly(pts_copper_mid_1, COPPER_MID),
        helix::nr_poly(pts_copper_mid_2, COPPER_MID),
        helix::nr_poly(pts_copper_mid_3, COPPER_MID),
        helix::nr_poly(pts_copper_mid_6, COPPER_MID),
        helix::nr_poly(pts_copper_mid_7, COPPER_MID),
        helix::nr_poly(pts_copper_mid_8, COPPER_MID),
        helix::nr_poly(pts_copper_mid_9, COPPER_MID),
        helix::nr_poly(pts_copper_mid_10, COPPER_MID),
        helix::nr_poly(pts_copper_mid_11, COPPER_MID),
        helix::nr_poly(pts_copper_mid_12, COPPER_MID),
        helix::nr_poly(pts_copper_mid_13, COPPER_MID),

        // Copper bright 3 (large body — background for screw features)
        helix::nr_poly(pts_copper_bright_3, COPPER_BRIGHT),

        // === SCREW FEATURES (on top of copper body) ===
        helix::nr_poly(pts_body_light_11, BODY_LIGHT), // Left screw highlight
        helix::nr_poly(pts_body_light_12, BODY_LIGHT), // Right screw highlight
        helix::nr_poly(pts_copper_mid_4, COPPER_MID),  // Left screw copper
        helix::nr_poly(pts_copper_mid_5, COPPER_MID),  // Right screw copper
        helix::nr_poly(pts_blue_7, BLUE_BRIGHT),       // Blue screw top
        helix::nr_poly(pts_blue_8, BLUE_BRIGHT),       // Blue screw
        helix::nr_poly(pts_blue_9, BLUE_BRIGHT),       // Blue screw
        helix::nr_poly(pts_blue_10, BLUE_BRIGHT),      // Blue screw bottom

        // === LARGE COPPER RING + LEGS (background for circle area) ===
        helix::nr_poly(pts_copper_bright_2, COPPER_BRIGHT), // Left leg column
        helix::nr_poly(pts_copper_bright_5, COPPER_BRIGHT), // Right leg column
        helix::nr_poly(pts_copper_bright_6, COPPER_BRIGHT), // Right screw ring surround
        helix::nr_poly(pts_copper_bright_8, COPPER_BRIGHT), // Main ring + legs frame

        // === CIRCLE + DRAGON DETAIL ===
        // Blue circle (background for dragon)
        helix::nr_poly(pts_blue_11, BLUE_BRIGHT),

        // Dragon detail + holes/cutouts (on top of circle)
        helix::nr_poly(pts_dark_0, DARK),
        helix::nr_poly(pts_dark_1, DARK),
        helix::nr_poly(pts_dark_2, DARK),
        helix::nr_poly(pts_dark_3, DARK),
        helix::nr_poly(pts_dark_4, DARK),
        helix::nr_poly(pts_dark_5, DARK),
        helix::nr_poly(pts_dark_6, DARK),
        helix::nr_poly(pts_dark_7, DARK),
        helix::nr_poly(pts_dark_8, DARK),
        helix::nr_poly(pts_dark_9, DARK),
        helix::nr_poly(pts_dark_10, DARK),
        helix::nr_poly(pts_dark_11, DARK),

        // === TOP DETAILS ===
        // Blue 12 (small feature near circle)
        helix::nr_poly(pts_blue_12, BLUE_BRIGHT),

        // Copper bright (small/medium shapes, after circle but before text)
        helix::nr_poly(pts_copper_bright_0, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_1, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_4, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_7, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_9, COPPER_BRIGHT),

        // "JabberWocky" vertical text (blue 13-20, on top of copper body)
        helix::nr_poly(pts_blue_13, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_14, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_15, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_16, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_17, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_18, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_19, BLUE_BRIGHT),
        helix::nr_poly(pts_blue_20, BLUE_BRIGHT),

        // Small copper details near text (rivets/markers)
        helix::nr_poly(pts_copper_bright_10, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_11, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_12, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_13, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_14, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_15, COPPER_BRIGHT),

        // Copper bright bottom (legs/lower assembly)
        helix::nr_poly(pts_copper_bright_16, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_17, COPPER_BRIGHT),
        helix::nr_poly(pts_copper_bright_18, COPPER_BRIGHT),

        // Highlights
        helix::nr_poly(pts_highlight_0, HIGHLIGHT),
        helix::nr_poly(pts_highlight_1, HIGHLIGHT),
    };

    helix::nr_draw_polygons(layer, polys, std::size(polys), palette, cx, cy, scale,
                            {DESIGN_CENTER_X, DESIGN_CENTER_Y});

    // Layer 13: Nozzle tip (shows filament color when loaded)
    {
        // Nozzle tip positioned below the copper assembly
        int32_t nozzle_top_y = cy + (int32_t)((1510 - DESIGN_CENTER_Y) * scale);
        int32_t nozzle_height = LV_MAX((int32_t)(40 * scale), 2);
        int32_t nozzle_top_width = LV_MAX((int32_t)(80 * scale), 4);
        int32_t nozzle_bottom_width = LV_MAX((int32_t)(30 * scale), 2);

        helix::nr_draw_filament_tip(layer, cx, nozzle_top_y, nozzle_top_width, nozzle_bottom_width,
                                    nozzle_height, filament, opa);
    }
}
