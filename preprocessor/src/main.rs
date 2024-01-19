/* Copyright (C) 2022-2024 Peter Lafreniere
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

use std::{
    cmp::{min, Ordering},
    fs::File,
    io::{Error, ErrorKind, Read, Write},
};

use binary_layout::prelude::*;
use clap::{Parser, ValueEnum};

const MAJOR_VERSION: u8 = 1;
const MINOR_VERSION: u8 = 0;
const PATCH_VERSION: u8 = 0;

// TODO: Make prettier
#[derive(Parser)]
#[clap(author, version, about, long_about = None)]
struct Args {
    // Positional
    /// CHIP-8 ROM
    #[clap(value_parser)]
    file: String,

    /// The target calculator
    #[clap(long, short, arg_enum, value_parser)]
    calc: Calc,

    /// On-calculator variable name, clipped to 8 characters (Optional)
    #[clap(long, short, value_parser)]
    var_name: Option<String>,

    /// On-calculator folder, clipped to 8 characters
    #[clap(default_value_t = String::from("main"), long, short, value_parser)]
    folder: String,

    /// The file to place output in (Optional)
    #[clap(long, short, value_parser)]
    output: Option<String>,
}

#[derive(Clone, ValueEnum)]
enum Calc {
    TI89,
    TI92P,
    V200,
}

define_layout!(header_size, LittleEndian, { size: u32 });

define_layout!(ch8_header, BigEndian, {
    signature: [u8; 8],
    fill1: u16,
    folder: [u8; 8],
    _desc: [u8; 40],
    fill2: [u8; 6],
    name: [u8; 8],
    fill3: u32, // "type" field in strhead.h, but in this case it's filler content.
    size: header_size::NestedView,
    fill4: [u8; 6],
    datasize: u16, // Checksum starts here
    maj_ver: u8,
    min_ver: u8,
    patch_ver: u8,
});

static OTH_CH8: [u8; 6] = [0, b'c', b'h', b'8', 0, 0xF8];

/// (Output path, stripped input filename)
fn get_filename(args: &Args) -> (String, String) {
    let mut path = args.file.as_str();
    path = path.strip_suffix(".ch8").unwrap_or(path);
    path = path.strip_suffix(".rom").unwrap_or(path);
    let (_, file) = path.rsplit_once('/').unwrap_or(("", path));

    (
        {
            let mut x = match &args.output {
                Some(s) => s,
                None => path,
            }
            .to_string();
            x.push_str(match args.calc {
                Calc::TI89 => ".89y",
                Calc::TI92P => ".9xy",
                Calc::V200 => ".v2y",
            });
            x
        },
        match &args.var_name {
            Some(s) => s,
            None => file,
        }
        .to_string(),
    )
}

fn strncpy<const N: usize>(dest: &mut [u8; N], src: &str) {
    for (i, b) in dest.iter_mut().enumerate() {
        *b = match src.as_bytes().get(i) {
            Some(v) => *v,
            None => 0,
        };
    }
}

/// Temporary workaround while we wait for write_all_vectored() to be stabilized.
/// This version guarantees that all data is written.
fn writev(dest: &mut File, src: &[&[u8]]) -> Result<(), Error> {
    for &buf in src.iter() {
        dest.write_all(buf)?;
    }
    Ok(())
}

/// See the calc code for a description of the compression format/algorithm.
fn compress(src: Vec<u8>) -> Vec<u8> {
    const COMPRESS_FLAG: u8 = 0xFF;
    const WINDOW_SIZE: usize = 1024;
    const MAX_COMPRESS_LEN: usize = 63;

    fn push_literal(out: &mut Vec<u8>, byte: u8) {
        if byte == COMPRESS_FLAG {
            out.push(COMPRESS_FLAG);
            out.push(0x00);
        } else {
            out.push(byte);
        }
    }

    let mut output = Vec::new();
    let mut i = 0;

    while i < src.len() {
        let window_start = i.saturating_sub(WINDOW_SIZE);
        let window = &src[window_start..i];

        let (j, len) = window
            .iter()
            .enumerate()
            .filter(|(_, &x)| x == src[i])
            .map(|(j, _)| {
                (
                    j,
                    src[(j + window_start)..]
                        .iter()
                        .zip(src[i..].iter())
                        .take_while(|(&a, &b)| a == b)
                        .count(),
                )
            })
            .max_by(|(_, lena), (_, lenb)| {
                if lena >= lenb {
                    Ordering::Greater
                } else {
                    Ordering::Less
                }
            })
            .unwrap_or((0, 0));

        let len = min(len, MAX_COMPRESS_LEN);

        if 3 < src[(j + window_start)..]
            .iter()
            .take(len)
            .fold(0, |a, &e| a + if e == COMPRESS_FLAG { 2 } else { 1 })
        {
            let offset = (window.len() - j - 1) as u16;
            output.push(COMPRESS_FLAG);
            output.push(((offset & 768) >> 2 | len as u16) as u8);
            output.push(offset as u8);
            i += len;
        } else {
            push_literal(&mut output, src[i]);
            i += 1;
        }
    }

    output
}

fn fill_header(
    mut header: ch8_header::View<&mut [u8]>,
    calc: Calc,
    folder: &str,
    name: &str,
    datasize: usize,
    ext_len: usize,
) -> Result<(), Error> {
    // Fill in all the filler data:
    header.maj_ver_mut().write(MAJOR_VERSION);
    header.min_ver_mut().write(MINOR_VERSION);
    header.patch_ver_mut().write(PATCH_VERSION);

    header.fill1_mut().write(0x0100);
    header
        .fill2_mut()
        .copy_from_slice(&[0x01, 0x00, 0x52, 0x00, 0x00, 0x00]);
    header.fill3_mut().write(0x1C000000);
    header
        .fill4_mut()
        .copy_from_slice(&[0xA5, 0x5A, 0x00, 0x00, 0x00, 0x00]);

    // Place simple values:
    header
        .datasize_mut()
        .write((datasize + 3 + ext_len + 3) as u16);
    header
        .size_mut()
        .size_mut()
        .write((ch8_header::SIZE.unwrap() + datasize + 5 + ext_len) as u32);
    header.signature_mut().copy_from_slice(
        match calc {
            Calc::TI89 => "**TI89**",
            _ => "**TI92P*",
        }
        .as_bytes(),
    );

    // Strings.
    strncpy(header.folder_mut(), folder);
    strncpy(header.name_mut(), name);
    Ok(())
}

fn compute_checksum(data: &[&[u8]]) -> [u8; 2] {
    data.iter()
        .flat_map(|v| *v)
        .fold(0u16, |a, x| a.wrapping_add((*x).into()))
        .to_le_bytes()
}

fn main() -> Result<(), Error> {
    let args = Args::parse();

    let (output, filename) = get_filename(&args);

    let mut rom = File::open(&args.file)?;
    let mut storage = Vec::new();
    rom.read_to_end(&mut storage)?;

    if storage.len() > 0x1000 {
        return Err(Error::from(ErrorKind::InvalidData));
    }

    let mut header_storage = [0u8; 91]; // sizeof(ti_header)

    let storage = compress(storage);

    fill_header(
        ch8_header::View::new(&mut header_storage),
        args.calc,
        &args.folder,
        &filename,
        storage.len(),
        "ch8".len(),
    )?;

    let mut f = File::create(output)?;

    writev(
        &mut f,
        &[
            &header_storage,
            &storage,
            &OTH_CH8,
            &compute_checksum(&[
                &header_storage[ch8_header::datasize::OFFSET..],
                &storage,
                &OTH_CH8,
            ]),
        ],
    )
}
