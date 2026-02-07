# Logo and Page Title Customization

The AsyncFsWebServer library provides flexible options for customizing the logo and page title displayed on the setup page (`/setup`).

## Page Title Customization

You can customize the page title using the `setSetupPageTitle()` method:

```cpp
server.setSetupPageTitle("My Custom Device");
```

This will display your custom title in the header of the setup page.

---

## Logo Customization

The library provides a simple, unified API for customizing the logo. The `setSetupPageLogo()` method supports both string literals (for SVG text) and binary arrays (for any image format).

### Using `setSetupPageLogo()` - Universal Method

**Supports:** PNG, JPEG, GIF, SVG (plain or gzipped)

This method automatically detects if data is gzip-compressed by checking magic bytes (0x1f 0x8b).

### Supported Image Formats and MIME Types

| Format | Extension | MIME Type | Use Case |
|--------|-----------|-----------|----------|
| PNG | `.png` | `image/png` | Raster images with transparency |
| JPEG | `.jpg` | `image/jpeg` or `image/jpg` | Photos and complex images |
| GIF | `.gif` | `image/gif` | Simple animations or logos |
| SVG (text) | `.svg` | `image/svg+xml` | Vector graphics (scalable) |
| SVG (gzipped) | `.svg.gz` | `image/svg+xml` | Compressed SVG (auto-detected) |

### Method Signatures

```cpp
// For binary image data (uint8_t array)
void setSetupPageLogo(const uint8_t* imageData, size_t imageSize, const char* mimeType = "image/png", bool overwrite = false);

// For string literals (SVG text)
void setSetupPageLogo(const char* svgText, bool overwrite = false);
```

### Examples by Format

**PNG Image:**

```cpp
const uint8_t myLogoPng[] PROGMEM = {
  0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // PNG magic bytes
  // ... rest of binary data
};

server.setSetupPageLogo(myLogoPng, sizeof(myLogoPng), "image/png");
```

**JPEG Image:**

```cpp
const uint8_t myLogoJpg[] PROGMEM = {
  0xFF, 0xD8, 0xFF, 0xE0, // JPEG magic bytes
  // ... rest of binary data
};

server.setSetupPageLogo(myLogoJpg, sizeof(myLogoJpg), "image/jpeg");
// Alternative MIME type: "image/jpg" (both work the same)
```

**GIF Image:**

```cpp
const uint8_t myLogoGif[] PROGMEM = {
  0x47, 0x49, 0x46, 0x38, 0x39, 0x61, // GIF89a magic bytes
  // ... rest of binary data
};

server.setSetupPageLogo(myLogoGif, sizeof(myLogoGif), "image/gif");
```

**SVG Text (string literal):**

```cpp
const char logo_svg[] = R"rawliteral(
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">
  <circle cx="32" cy="32" r="30" fill="#0066cc"/>
  <text x="32" y="40" font-size="24" fill="white" text-anchor="middle">ESP</text>
</svg>
)rawliteral";

server.setSetupPageLogo(logo_svg);
// MIME type is automatically set to "image/svg+xml"
```

**SVG Gzipped (binary array):**

```cpp
// Pre-compressed SVG with gzip
const uint8_t logo_svg_gz[] PROGMEM = {
  0x1f, 0x8b, // Gzip magic bytes (automatically detected!)
  // ... rest of gzipped data
};

server.setSetupPageLogo(logo_svg_gz, sizeof(logo_svg_gz), "image/svg+xml");
// Will automatically save as /config/logo.svg.gz due to gzip detection
```

**How it works:**
- Binary data is saved directly to `/config/logo.{ext}`
- Gzip compression is detected automatically by checking magic bytes (0x1f 0x8b)
- If gzipped, `.gz` extension is added automatically (e.g., `/config/logo.svg.gz`)
- Server handles content-encoding negotiation automatically
- No encoding or conversion needed
- Raw image data is written to filesystem as-is

**Advantages:**
- ✅ Single unified API for all formats
- ✅ Supports both string literals and binary arrays
- ✅ Automatic gzip detection
- ✅ No conversion overhead
- ✅ Smaller code footprint
- ✅ Supports multiple formats (PNG, JPEG, GIF, SVG)
- ✅ Image stored in firmware (no manual filesystem upload)
- ✅ Direct binary write for efficiency

**Tools for converting images to C arrays:**

**Built-in Python script (recommended):**

Located in `built-in-webpages/setup/build_setup/image_to_c_array.py`

```bash
# Basic usage - creates custom_logo.h automatically
python image_to_c_array.py logo.png custom_logo

# Specify output filename
python image_to_c_array.py logo.png custom_logo logo.h
```

**Other tools:**
- Online: https://notisrac.github.io/FileToCArray/ ⚠️ **Note**: Make sure to upload the actual PNG/JPEG/GIF file, not a bitmap conversion
- Command line (Linux/Mac): `xxd -i logo.png > logo.h`
- Manual Python script:
  ```python
  with open('logo.png', 'rb') as f:
      data = f.read()
      print('const uint8_t myLogo[] PROGMEM = {')
      print(','.join(f'0x{b:02X}' for b in data))
      print('};')
  ```

---

## Logo Storage and Priority

Custom logos are saved to the filesystem and referenced in the configuration JSON (`/config/setup.json`). The JavaScript in the setup page reads the logo path from the configuration and updates the `<img>` element accordingly.

**Logo path storage:**
- Methods like `setLogoSVG()` and `setLogoFromImage()` save:
  1. The logo file to the filesystem (e.g., `/config/logo.svg`, `/config/logo.png`)
  2. The file path to the `img-logo` key in the configuration JSON

**Client-side handling:**
- `setLogoFromImage()` automatically saves:
  1. The logo file to the filesystem (e.g., `/config/logo.svg`, `/config/logo.png`, `/config/logo.svg.gz`)
  2. The file path to the `img-logo` key in the configuration JSON
- Gzip compression is detected automatically by checking magic bytes (0x1f 0x8b)

**Client-side handling:**
- JavaScript reads `cfg['img-logo']` and sets `document.querySelector('img').src`
- Default static file handlers serve the logo file automatically
- AsyncWebServer handles gzip content-encoding negotiation transparent

---

## Complete Example

```cpp
#include <AsyncFsWebServer.h>

// Define your logo (choose one method):

// Option 1: SVG
const char logo_svg[] = R"rawliteral(
<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">
  <circle cx="32" cy="32" r="28" fill="#ff6600"/>
  <text x="32" y="42" font-size="32" fill="white" text-anchor="middle">⚡</text>
</svg>
)rawliteral";

// Option 2: Binary PNG
const uint8_t logo_png[] PROGMEM = {
  0x89, 0x50, 0x4E, 0x47, /* ... */
};

void setup() {
  
  // Customize page title
  server.setSetupPageTitle("Smart Device Control");
  
  // Set logo (choose one):
  server.setSetupPageLogo(logo_svg);  // SVG text
  // OR
  server.setSetupPageLogo(logo_png, sizeof(logo_png), "image/png");  // Binary image
  
  // Save configuration
  server.closeSetupConfiguration();
}
```

---

## Overwriting Existing Logos

Both overloads accept an optional `overwrite` parameter (default: `false`):

```cpp
// This will NOT overwrite if logo file already exists
server.setSetupPageLogo(pngData, sizeof(pngData), "image/png", false);

// Force overwrite existing logo
server.setSetupPageLogo(pngData, sizeof(pngData), "image/png", true);

// Same for string literals (SVG text)
server.setSetupPageLogo(logo_svg, false);  // don't overwrite
server.setSetupPageLogo(logo_svg, true);   // force overwrite
```

---

## Best Practices

1. **Use SVG when possible** - Smallest size, best quality, fully scalable
2. **Keep images small** - Recommended size: 64x64 to 128x128 pixels
3. **Optimize before embedding** - Compress PNG/JPEG before converting to array
4. **Use PROGMEM** - Always store logo data in PROGMEM to save RAM
5. **Test on actual device** - Verify the logo displays correctly on ESP hardware

---

## Troubleshooting

**Logo not displaying:**
- Check that filesystem is initialized before calling logo methods
- Verify the image format matches the MIME type
- Ensure base64 string is complete (no line breaks/spaces)
- Check browser console for errors

**Pre-compress SVG with gzip (automatically detected by library)
- Reduce image dimensions
- Use SVG instead of raster formats
- Compress PNG/JPEG before embedding
- Consider gzipping SVG files

**Memory issues:**
- Use PROGMEM for all logo data
- Reduce image size/quality
- Use gzipped SVG for large graphics
