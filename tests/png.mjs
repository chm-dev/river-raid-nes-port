/*
 * Minimal PNG reader for smoke-test screenshots.
 *
 * Supports the 8-bit RGB/RGBA, non-interlaced files produced by Mesen's
 * screenshot tool.  Keeps the test dependency-free (Node's zlib only).
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

const PNG_SIGNATURE = 0x89504e47;

export function readPng(file) {
    const buf = fs.readFileSync(file);

    if (buf.readUInt32BE(0) !== PNG_SIGNATURE) {
        throw new Error(`${file}: not a PNG`);
    }

    let off = 8;
    let width = 0;
    let height = 0;
    let bitDepth = 0;
    let colorType = 0;
    let interlace = 0;
    const idat = [];

    while (off + 12 <= buf.length) {
        const len = buf.readUInt32BE(off);
        const type = buf.toString('ascii', off + 4, off + 8);
        const data = buf.subarray(off + 8, off + 8 + len);

        if (type === 'IHDR') {
            width = data.readUInt32BE(0);
            height = data.readUInt32BE(4);
            bitDepth = data[8];
            colorType = data[9];
            interlace = data[12];
        } else if (type === 'IDAT') {
            idat.push(data);
        } else if (type === 'IEND') {
            break;
        }

        off += 12 + len;
    }

    const channels = colorType === 2 ? 3 : colorType === 6 ? 4 : 0;
    if (!width || !height || !channels || bitDepth !== 8 || interlace !== 0) {
        throw new Error(`${file}: unsupported PNG (${width}x${height}, ` +
            `depth ${bitDepth}, color ${colorType}, interlace ${interlace})`);
    }

    const stride = width * channels;
    const raw = zlib.inflateSync(Buffer.concat(idat));
    const out = Buffer.alloc(height * stride);
    let pos = 0;

    for (let y = 0; y < height; y++) {
        const filter = raw[pos++];
        const inRow = raw.subarray(pos, pos + stride);
        pos += stride;
        const outRow = out.subarray(y * stride, (y + 1) * stride);
        const prev = y ? out.subarray((y - 1) * stride, y * stride) : null;

        for (let x = 0; x < stride; x++) {
            const a = x >= channels ? outRow[x - channels] : 0;
            const b = prev ? prev[x] : 0;
            const c = prev && x >= channels ? prev[x - channels] : 0;
            let v = inRow[x];

            switch (filter) {
                case 0:
                    break;
                case 1:
                    v = (v + a) & 0xff;
                    break;
                case 2:
                    v = (v + b) & 0xff;
                    break;
                case 3:
                    v = (v + ((a + b) >> 1)) & 0xff;
                    break;
                case 4: {
                    const p = a + b - c;
                    const pa = Math.abs(p - a);
                    const pb = Math.abs(p - b);
                    const pc = Math.abs(p - c);
                    const pred = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                    v = (v + pred) & 0xff;
                    break;
                }
                default:
                    throw new Error(`${file}: bad PNG filter ${filter}`);
            }

            outRow[x] = v;
        }
    }

    return {
        width,
        height,
        data: out,
        pixel(x, y) {
            const i = (y * width + x) * channels;
            return [out[i], out[i + 1], out[i + 2]];
        },
    };
}

export function countRow(image, y, predicate) {
    let n = 0;
    for (let x = 0; x < image.width; x++) {
        if (predicate(...image.pixel(x, y))) n++;
    }
    return n;
}

export function rowMatches(image, y, predicate) {
    return countRow(image, y, predicate) === image.width;
}
