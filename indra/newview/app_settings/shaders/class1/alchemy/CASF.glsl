/** 
 * @file CASF.glsl
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2021, Rye Mutt<rye@alchemyviewer.org>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * $/LicenseInfo$
 */

#extension GL_ARB_shader_texture_lod : enable
#extension GL_EXT_gpu_shader4 : enable

/*[EXTRA_CODE_HERE]*/

#ifdef DEFINE_GL_FRAGCOLOR
out vec4 frag_color;
#else
#define frag_color gl_FragColor
#endif

in vec2 vary_fragcoord;

uniform sampler2D tex0;

uniform vec3 sharpen_params = vec3(1.0, 0.5, 0.0);

vec3 linear_to_srgb(vec3 cl);

void main()
{
    // LICENSE
    // =======
    // Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
    // -------
    // Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
    // files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
    // modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
    // Software is furnished to do so, subject to the following conditions:
    // -------
    // The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
    // Software.
    // -------
    // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    // WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
    // COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    // ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
    
    // fetch a 3x3 neighborhood around the pixel 'e',
    //  a b c
    //  d(e)f
    //  g h i
    vec4 inputColor = textureLod(tex0, vary_fragcoord, 0.0f);
    float alpha = inputColor.a;

    vec3 a = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2(-1,-1)).rgb;
    vec3 b = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2( 0,-1)).rgb;
    vec3 c = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2( 1,-1)).rgb;
    vec3 d = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2(-1, 0)).rgb;
    vec3 e = inputColor.rgb;
    vec3 f = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2( 1, 0)).rgb;
    vec3 g = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2(-1, 1)).rgb;
    vec3 h = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2( 0, 1)).rgb;
    vec3 i = textureLodOffset(tex0, vary_fragcoord, 0.0f, ivec2( 1, 1)).rgb;

    // Soft min and max.
    //  a b c             b
    //  d e f * 0.5  +  d e f * 0.5
    //  g h i             h
    // These are 2.0x bigger (factored out the extra multiply).

    vec3 mnRGB = min(min(min(d, e), min(f, b)), h);
    vec3 mnRGB2 = min(mnRGB, min(min(a, c), min(g, i)));
    mnRGB += mnRGB2;

    vec3 mxRGB = max(max(max(d, e), max(f, b)), h);
    vec3 mxRGB2 = max(mxRGB, max(max(a, c), max(g, i)));
    mxRGB += mxRGB2;

    // Smooth minimum distance to signal limit divided by smooth max.
    vec3 rcpMxRGB = vec3(1) / mxRGB;
    vec3 ampRGB = clamp((min(mnRGB, 2.0-mxRGB) * rcpMxRGB), 0, 1);

    // Shaping amount of sharpening.
    ampRGB = inversesqrt(ampRGB);

    float peak = 8.0 - 3.0 * sharpen_params.y;
    vec3 wRGB = -vec3(1)/(ampRGB * peak);
    vec3 rcpWeightRGB = vec3(1)/(1.0 + 4.0 * wRGB);

    //                          0 w 0
    //  Filter shape:           w 1 w
    //                          0 w 0  

    vec3 window = (b + d) + (f + h);
    vec3 outColor = clamp((window * wRGB + e) * rcpWeightRGB, 0, 1);
    outColor = mix(e, outColor, sharpen_params.x);

    frag_color = vec4(outColor,alpha);
}

