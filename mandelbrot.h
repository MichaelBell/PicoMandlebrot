// Fixed point with 4 bits to the left of the point.
// Range [-8,8) with precision 2^-28
typedef int32_t fixed_pt_t;

// Make a fixed_pt_t from an int or float.
fixed_pt_t make_fixed(int32_t x);
fixed_pt_t make_fixedf(float x);

// Generate a section of the fractal into buff
// Result written to buff is 0 for inside Mandelbrot set
// Otherwise iteration of escape minus min_iter (clamped to 1)
uint16_t generate(uint8_t* buff, int16_t rows, int16_t cols, 
                  uint16_t max_iter, uint16_t min_iter, bool use_cycle_check,
                  fixed_pt_t minx, fixed_pt_t maxx,
                  fixed_pt_t miny, fixed_pt_t maxy);
