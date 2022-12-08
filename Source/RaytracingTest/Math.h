﻿#pragma once

template<typename T>
inline T nextPowerOfTwo(T value)
{
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    ++value;

    return value;
}

inline uint8_t quantizeSint8(float v)
{
    if (v >= 0)
    {
		if (v >= 1.0f)
			return static_cast<uint8_t>(static_cast<int8_t>(127));
		else
			return static_cast<uint8_t>(static_cast<int8_t>(v * 127.0f));
    }
	else
	{
		if (v <= -1.0f)
			return static_cast<uint8_t>(static_cast<int8_t>(-128));
		else
			return static_cast<uint8_t>(static_cast<int8_t>(v * 128.0f));
	}
}

inline uint8_t quantizeUint8(float v)
{
	if (v >= 1.0f)
		return 0xFF;
	else if (v <= 0.0f)
		return 0x00;
	else
		return (uint8_t)(v * 0xFF);
}

// From https://github.com/zeux/meshoptimizer/blob/master/src/meshoptimizer.h
inline uint16_t quantizeHalf(float v)
{
	union { float f; unsigned int ui; } u = { v };
	unsigned int ui = u.ui;

	int s = (ui >> 16) & 0x8000;
	int em = ui & 0x7fffffff;

	/* bias exponent and round to nearest; 112 is relative exponent bias (127-15) */
	int h = (em - (112 << 23) + (1 << 12)) >> 13;

	/* underflow: flush to zero; 113 encodes exponent -14 */
	h = (em < (113 << 23)) ? 0 : h;

	/* overflow: infinity; 143 encodes exponent 16 */
	h = (em >= (143 << 23)) ? 0x7c00 : h;

	/* NaN; note that we convert all types of NaN to qNaN */
	h = (em > (255 << 23)) ? 0x7e00 : h;

	return (uint16_t)(s | h);
}