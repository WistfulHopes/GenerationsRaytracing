﻿#pragma once

enum
{
    D3DFMT_A2M0 = MAKEFOURCC('A', '2', 'M', '0'),
    D3DFMT_A2M1 = MAKEFOURCC('A', '2', 'M', '1'),
    D3DFMT_ATI1 = MAKEFOURCC('A', 'T', 'I', '1'),
    D3DFMT_ATI2 = MAKEFOURCC('A', 'T', 'I', '2'),
    D3DFMT_ATOC = MAKEFOURCC('A', 'T', 'O', 'C'),
    D3DFMT_DF16 = MAKEFOURCC('D', 'F', '1', '6'),
    D3DFMT_DF24 = MAKEFOURCC('D', 'F', '2', '4'),
    D3DFMT_GET1 = MAKEFOURCC('G', 'E', 'T', '1'),
    D3DFMT_GET4 = MAKEFOURCC('G', 'E', 'T', '4'),
    D3DFMT_INST = MAKEFOURCC('I', 'N', 'S', 'T'),
    D3DFMT_INTZ = MAKEFOURCC('I', 'N', 'T', 'Z'),
    D3DFMT_NULL = MAKEFOURCC('N', 'U', 'L', 'L'),
    D3DFMT_NVDB = MAKEFOURCC('N', 'V', 'D', 'B'),
    D3DFMT_RAWZ = MAKEFOURCC('R', 'A', 'W', 'Z')
};

struct Format
{
    static nvrhi::Format convert(unsigned int format);
};