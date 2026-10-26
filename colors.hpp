//
// colors.hpp
#pragma once
#include <stdint.h>
#include "sdl_wrapper.hpp"

#pragma once 

//AI assisted color generator made colors
// Fixed palette of 256 maximally-distinct, vibrant colors, computed once //
 // offline in OKLCH/OKLab space and baked into this table. //
 //all the pitfalls that were avoided and techniques used //
// 1. Blue-arc curvature (violet-shift bug): distance is measured in OKLab //
 // Cartesian (a,b), not by walking straight lines through the raw hue arc, //
 // and the 250-290 deg blue->violet region gets an extra tangential //
 // weight so the sampler can't collapse many samples into "looks purple". //
 // 2/7. Gamut boundary pinning / non-convex clipping: candidates are //
 // generated then tested by literally converting to linear sRGB and //
 // rejecting anything out of [0,1]^3 -- no clamping-to-the-wall, so there //
 // are no artificial clumps stacked on the gamut boundary. //
 // 3. Dichromatic axis collapse: every candidate pair is scored under a //
 // simulated protanopia/deuteranopia projection (red-green axis //
 // compressed) as well as normal vision, and the WORST (smallest) of //
 // those distances is what farthest-point search actually optimizes. //
 // 4. Simultaneous contrast: the final 256 are re-ordered with a greedy //
 // max-min walk so consecutive table entries are always far apart -- //
 // the most common way this table gets used (adjacent legend/chart //
 // entries) won't suffer lateral-inhibition muddying. //
 // 5. Cylindrical seam discontinuity: because distance is computed in //
 // Cartesian OKLab (a,b) rather than raw hue-angle degrees, there is no //
 // 0/360 wraparound to get wrong in the first place. //
 // 6. Polar volume shrinkage: chroma is sampled as C = Cmax*sqrt(u), the //
 // standard disk-area correction, instead of uniform C, so candidates //
 // aren't overwhelmingly dumped near C=0 (gray). //
 // 8. Post-quantization collisions: candidates are only accepted into the //
 // table if their rounded 8-bit hex hasn't already been used, so no two //
 // entries below can ever print the same hex code. //
 // 9/10. H-K inflation & Bezold-Brucke compression: lightness bounds are //
 // hue-dependent (blues/violets get a lower floor since they read //
 // brighter than their L; yellow/greens get a lower ceiling so they don't //
 // wash out to pastel; nothing goes above L=0.85 at all). //
 // 11. Seed determinism: farthest-point sampling was run from several //
 // different random starting seeds and the run with the best worst-case //
 // (minimum) pairwise spacing was kept, instead of trusting one arbitrary //
 // starting point's spatial bias.

namespace colors {
    class Rgb {
    public:
        uint8_t r, g, b;
    };

    constexpr int32_t DistinctColorCount = 256;
    constexpr Rgb DistinctColors[DistinctColorCount] = {
        {112, 152, 103}, {3, 8, 253}, {170, 206, 2}, {67, 14, 254}, {251, 179, 99}, {71, 27, 223},
        {252, 189, 134}, {9, 54, 239}, {67, 216, 45}, {57, 53, 210}, {241, 165, 63}, {66, 52, 224},
        {22, 225, 100}, {43, 69, 255}, {131, 195, 31}, {25, 76, 228}, {237, 184, 123}, {7, 70, 195},
        {82, 237, 169}, {115, 35, 189}, {245, 181, 145}, {133, 7, 244}, {67, 196, 4}, {134, 43, 254},
        {236, 154, 64}, {52, 78, 209}, {255, 184, 185}, {129, 16, 159}, {67, 235, 207}, {2, 85, 179},
        {244, 164, 102}, {138, 13, 222}, {46, 212, 86}, {19, 94, 237}, {250, 130, 33}, {95, 86, 245},
        {143, 171, 1}, {157, 47, 254}, {249, 107, 30}, {148, 10, 203}, {3, 229, 155}, {123, 43, 144},
        {236, 183, 167}, {11, 93, 197}, {26, 222, 126}, {147, 8, 183}, {231, 175, 131}, {140, 32, 164},
        {72, 229, 186}, {104, 70, 122}, {214, 193, 253}, {87, 82, 100}, {248, 178, 221}, {90, 88, 92},
        {14, 229, 239}, {61, 91, 152}, {252, 173, 193}, {47, 95, 180}, {82, 226, 214}, {10, 100, 216},
        {252, 141, 94}, {149, 44, 225}, {48, 197, 69}, {176, 38, 244}, {107, 169, 19}, {165, 75, 254},
        {255, 77, 4}, {51, 112, 241}, {203, 162, 81}, {9, 104, 200}, {48, 219, 145}, {170, 6, 201},
        {248, 164, 150}, {139, 60, 166}, {108, 216, 167}, {120, 76, 138}, {250, 160, 243}, {99, 88, 117},
        {116, 207, 253}, {77, 131, 8}, {249, 148, 252}, {98, 97, 104}, {108, 214, 216}, {30, 109, 180},
        {38, 211, 129}, {55, 106, 218}, {251, 114, 69}, {69, 120, 241}, {157, 143, 1}, {212, 32, 254},
        {39, 160, 0}, {236, 1, 253}, {16, 151, 11}, {253, 25, 253}, {218, 13, 7}, {238, 139, 254},
        {212, 66, 0}, {234, 125, 254}, {193, 65, 48}, {121, 206, 236}, {91, 101, 133}, {73, 219, 191},
        {120, 86, 159}, {239, 168, 194}, {157, 56, 183}, {231, 155, 117}, {37, 112, 203}, {55, 199, 102},
        {190, 21, 226}, {58, 185, 70}, {48, 117, 222}, {235, 66, 2}, {111, 127, 247}, {253, 56, 48},
        {210, 0, 230}, {22, 174, 60}, {184, 16, 205}, {249, 149, 140}, {13, 120, 184}, {253, 155, 167},
        {135, 86, 150}, {242, 162, 218}, {109, 107, 110}, {217, 164, 236}, {19, 136, 74}, {239, 95, 254},
        {30, 146, 62}, {59, 151, 255}, {225, 55, 48}, {54, 164, 254}, {8, 158, 59}, {15, 181, 254},
        {200, 46, 74}, {98, 194, 244}, {147, 112, 75}, {12, 193, 252}, {214, 60, 68}, {253, 122, 233},
        {196, 51, 95}, {23, 205, 230}, {125, 96, 133}, {19, 215, 160}, {28, 123, 204}, {253, 90, 65},
        {68, 129, 229}, {240, 129, 102}, {193, 40, 212}, {147, 160, 79}, {226, 18, 229}, {76, 161, 77},
        {69, 144, 238}, {234, 62, 66}, {241, 33, 230}, {101, 179, 102}, {196, 33, 192}, {101, 200, 146},
        {19, 123, 170}, {243, 153, 200}, {40, 124, 155}, {74, 207, 214}, {195, 30, 139}, {14, 210, 192},
        {175, 61, 174}, {193, 166, 130}, {59, 130, 211}, {109, 186, 116}, {216, 11, 209}, {237, 113, 98},
        {19, 129, 190}, {253, 138, 169}, {180, 59, 159}, {98, 199, 165}, {86, 121, 131}, {239, 144, 217},
        {46, 138, 98}, {223, 128, 236}, {217, 40, 90}, {123, 157, 241}, {244, 83, 83}, {0, 146, 222},
        {12, 176, 98}, {45, 156, 232}, {184, 109, 86}, {255, 53, 230}, {34, 150, 93}, {21, 186, 227},
        {207, 3, 114}, {188, 169, 200}, {69, 133, 118}, {244, 118, 213}, {128, 128, 105}, {72, 170, 227},
        {236, 31, 104}, {223, 51, 213}, {247, 119, 127}, {67, 127, 175}, {252, 127, 149}, {66, 134, 193},
        {120, 176, 128}, {215, 2, 176}, {36, 196, 156}, {56, 130, 148}, {18, 197, 181}, {206, 40, 132},
        {63, 191, 212}, {216, 42, 114}, {245, 89, 215}, {63, 152, 112}, {206, 102, 218}, {254, 84, 113},
        {194, 81, 196}, {23, 177, 129}, {121, 139, 205}, {248, 47, 104}, {43, 164, 209}, {156, 143, 120},
        {231, 84, 203}, {36, 149, 124}, {253, 110, 184}, {184, 75, 158}, {249, 120, 166}, {61, 138, 166},
        {246, 109, 141}, {50, 146, 193}, {75, 180, 149}, {232, 14, 178}, {108, 178, 166}, {67, 138, 147},
        {255, 84, 194}, {149, 115, 131}, {122, 164, 210}, {174, 117, 121}, {235, 51, 193}, {5, 164, 133},
        {97, 177, 192}, {231, 16, 138}, {244, 105, 164}, {1, 151, 177}, {246, 89, 142}, {151, 132, 188},
        {253, 49, 128}, {37, 171, 193}, {242, 29, 129}, {253, 62, 179}, {47, 150, 148}, {72, 175, 170},
        {201, 80, 159}, {130, 157, 146}, {244, 47, 176}, {215, 100, 145}, {63, 159, 179}, {252, 63, 159},
        {174, 117, 164}, {143, 150, 174}, {247, 19, 151}, {99, 152, 158}
    };

    inline Rgb NextDistinctColor(uint64_t* rng_state) {
        int32_t index = sdl::rand_below(rng_state, DistinctColorCount);
        return DistinctColors[index];
    }
}
