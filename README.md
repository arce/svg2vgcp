# svg2vgcp

A command-line tool written in C++ that converts SVG files into **VGCP 3.3** protocol commands.

## Features

- Converts all common SVG elements to their VGCP equivalents
- Parses inline `style` attributes and embedded `<style>` blocks (CSS)
- Supports color formats: hex (`#RGB`, `#RRGGBB`), `rgb()`, `rgba()`, and named colors
- Handles SVG `<symbol>` / `<use>` reuse patterns
- Inherits and propagates fill, stroke, opacity, and transforms through group hierarchies
- Configurable output: shorthands, indentation, ID preservation, group flattening

## Supported SVG Elements

| SVG Element | VGCP Command |
|-------------|--------------|
| `<svg>` | `VIEW` |
| `<style>` | `STY` |
| `<g>` | `GRP` |
| `<rect>` | `RCT` |
| `<circle>` | `CIR` |
| `<ellipse>` | `ELL` |
| `<line>` | `LIN` |
| `<path>` | `PAT` |
| `<polygon>` | `PGN` |
| `<polyline>` | `PLN` |
| `<text>` | `TXT` |
| `<image>` | `IMG` |
| `<symbol>` | `SYM` |
| `<use>` | `USE` |

## Requirements

- C++17 or later
- [pugixml](https://pugixml.org/) — bundled in the `pugixml/` directory (header-only mode)

## Building

```bash
g++ -std=c++17 -O2 -o svg2vgcp svg2vgcp.cpp
```

On Windows (MSVC):

```cmd
cl /std:c++17 /O2 svg2vgcp.cpp /Fe:svg2vgcp.exe
```

## Usage

```
svg2vgcp [options] input.svg [output.vgcp]
```

If no output file is specified, the result is printed to `stdout`.

### Options

| Option | Description |
|--------|-------------|
| `--no-shorthands` | Use full attribute names (`stroke-width`, `opacity`, …) instead of abbreviations (`sw`, `op`, …) |
| `--flatten` | Flatten group hierarchies and propagate fill, stroke, opacity, and transforms to child elements |
| `--no-preserve-ids` | Generate new sequential IDs (`e1`, `e2`, …) instead of keeping the original SVG IDs |
| `--indent` | Pretty-print output with semantic indentation |
| `--help` | Show usage information |

### Examples

Convert and print to stdout:

```bash
./svg2vgcp icon.svg
```

Convert and save to file:

```bash
./svg2vgcp icon.svg icon.vgcp
```

Convert with full attribute names and indented output:

```bash
./svg2vgcp --no-shorthands --indent icon.svg icon.vgcp
```

Flatten all groups and regenerate IDs:

```bash
./svg2vgcp --flatten --no-preserve-ids icon.svg flat.vgcp
```

## Output Format

The converter emits one VGCP command per line. The first line is always a `VIEW` declaration followed by optional `STY` style rules, then the element commands.

**Example input (`icon.svg`):**

```xml
<svg width="100" height="100" xmlns="http://www.w3.org/2000/svg">
  <rect id="bg" x="0" y="0" width="100" height="100" fill="#eee"/>
  <circle id="dot" cx="50" cy="50" r="20" fill="blue" stroke="black" stroke-width="2"/>
</svg>
```

**Example output:**

```
VIEW w=100 h=100 bg=#eeeeee
RCT id=bg fill=#eeeeee x=0 y=0 w=100 h=100
CIR id=dot fill=#0000ff stroke=#000000 sw=2 cx=50 cy=50 r=20
```

## Attribute Shorthands

By default the converter uses abbreviated parameter names to keep output compact:

| Full name | Shorthand |
|-----------|-----------|
| `stroke-width` | `sw` |
| `transform` | `tr` |
| `opacity` | `op` |
| `font-size` | `fs` |
| `font-family` | `ff` |
| `font-weight` | `fw` |
| `text-anchor` | `ta` |
| `preserveAspectRatio` | `pr` |

Use `--no-shorthands` to emit the full names instead.

## Project Structure

```
svg2vgcp/
├── svg2vgcp.cpp      # Main converter source
└── pugixml/
    ├── pugixml.hpp   # pugixml header
    ├── pugixml.cpp   # pugixml implementation
    └── pugiconfig.hpp
```

## License

This project bundles [pugixml](https://pugixml.org/), which is distributed under the MIT license.
