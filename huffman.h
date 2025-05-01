/* bmplib - huffman.h
 *
 * Copyright (c) 2024, 2025, Rupert Weber.
 *
 * This file is part of bmplib.
 * bmplib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <https://www.gnu.org/licenses/>
 */

int huff_decode(BMPREAD_R rp, int black);
void huff_fillbuf(BMPREAD_R rp);

bool huff_encode(BMPWRITE_R wp, int val, bool black);
bool huff_encode_eol(BMPWRITE_R wp);
bool huff_encode_rtc(BMPWRITE_R wp);
bool huff_flush(BMPWRITE_R wp);
