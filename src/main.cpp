#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <set>

#include <boost/filesystem.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_ERRORS_H

#include <cereal/cereal.hpp>

#include <gpc/fonts/RasterizedGlyphCBox.hpp>
#include <gpc/fonts/GlyphRange.hpp>
#include <gpc/fonts/RasterizedFont.hpp>

typedef std::pair<std::string, std::string> NameValuePair;

static auto splitParam(const std::string &param) -> NameValuePair {

    auto p = param.find_first_of('=');
    std::string name, value;

    if (p != std::string::npos) {
        name = param.substr(0, p);
        value = param.substr(p + 1);
    }
    else {
        name = param;
    }

    return std::move( NameValuePair(name, value) );
}

static auto findFontFile(const std::string &file) -> std::string {

    using std::string;
    using std::runtime_error;
    using namespace boost::filesystem;

    path file_path(file);

    // If the unaltered path leads to an existing file, there's nothing to do
    if (exists(file_path)) return file_path.string();

    // If the path is not absolute, try OS-dependent prefixes
    if (!file_path.is_absolute()) {
#ifdef _WIN32
        const char *system_root = getenv("SystemRoot");
        if (system_root == nullptr) system_root = "C:\\Windows";
        path full_path = path(system_root) / "Fonts" / file_path;
        if (!exists(full_path)) throw runtime_error(string("Couldn't find font file \"") + file + "\" at any known location");
        return full_path.string();
#else
#pragma warn "Font file search paths not yet implemented for any platform except MS Windows (Tm)"
#endif
    }
    
    throw runtime_error(string("Font file \"") + file + "\" doesn't exist");
}

int main(int argc, const char *argv[])
{
    using namespace std;
	using gpc::fonts::RasterizedGlyphCBox;
	using gpc::fonts::GlyphRange;
	using gpc::fonts::RasterizedFont;

    int exit_code = -1;

    try {

        string font_file;
		set<uint16_t> sizes;

        // Get command-line arguments
        for (auto i = 1; i < argc; i ++) {
            auto name_value = splitParam(argv[i]);
            if (name_value.first == "file") {
                //cout << "file = " << name_value.second << endl;
				//cout << "font file path = " << font_file << endl;
				font_file = findFontFile(name_value.second);
            }
			else if (name_value.first == "size") {
				sizes.insert(stoi(name_value.second));
			}
            else {
                throw runtime_error(string("invalid parameter \"") + argv[i] + "\"");
            }
        }

        if (font_file.empty()) throw runtime_error("No font file specified!");
		if (sizes.empty()) throw runtime_error("No sizes specified!");

        FT_Library library;
        FT_Error fterror;

        fterror = FT_Init_FreeType(&library);
        if (fterror) throw runtime_error("FreeType library failed to initialize");

        FT_Face face;
        fterror = FT_New_Face(library, font_file.c_str(), 0, &face);
        if (fterror) throw runtime_error("Couldn't load or use font file");

        cout << "Font file contains " << face->num_faces << " face(s)." << endl;
        cout << "This face contains " << face->num_glyphs << " glyphs." << endl;
		cout << "Face flags:";
		if ((face->face_flags & FT_FACE_FLAG_SCALABLE        ) != 0) cout << " scalable";
		if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES     ) != 0) cout << " fixed sizes";
		if ((face->face_flags & FT_FACE_FLAG_FIXED_WIDTH     ) != 0) cout << " fixed width";
		if ((face->face_flags & FT_FACE_FLAG_SFNT            ) != 0) cout << " SFNT";
		if ((face->face_flags & FT_FACE_FLAG_HORIZONTAL      ) != 0) cout << " horizontal";
		if ((face->face_flags & FT_FACE_FLAG_VERTICAL        ) != 0) cout << " vertical";
		if ((face->face_flags & FT_FACE_FLAG_KERNING         ) != 0) cout << " kerning";
		if ((face->face_flags & FT_FACE_FLAG_FAST_GLYPHS     ) != 0) cout << " fast_glyphs";
		if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) != 0) cout << " multiple_masters";
		if ((face->face_flags & FT_FACE_FLAG_GLYPH_NAMES     ) != 0) cout << " glyph_names";
		if ((face->face_flags & FT_FACE_FLAG_EXTERNAL_STREAM ) != 0) cout << " external stream";
		if ((face->face_flags & FT_FACE_FLAG_HINTER          ) != 0) cout << " hinter";
		if ((face->face_flags & FT_FACE_FLAG_CID_KEYED       ) != 0) cout << " CID_keyed";
		if ((face->face_flags & FT_FACE_FLAG_TRICKY          ) != 0) cout << " tricky";
		if ((face->face_flags & FT_FACE_FLAG_COLOR           ) != 0) cout << " color";
		cout << endl;
		cout << "Style flags: ";
		if ((face->style_flags & FT_STYLE_FLAG_BOLD          ) != 0) cout << " bold";
		if ((face->style_flags & FT_STYLE_FLAG_ITALIC        ) != 0) cout << " italic";
		cout << endl;
        if ((face->face_flags & FT_FACE_FLAG_SCALABLE)) cout << "This font is scalable" << endl;
        cout << "Units per EM: " << face->units_per_EM << endl;
        cout << "Number of fixed sizes: " << face->num_fixed_sizes << endl;

        //fterror = FT_Set_Char_Size(face, 0, 16*64, 300, 300);
        //if (fterror) throw runtime_error("Failed to set character size (in 64ths of point)");

		RasterizedFont rast_font;
		rast_font.variants.resize(sizes.size()); // TODO: support styles, not just sizes

		uint32_t glyph_count = 0, missing_count = 0;
		uint32_t next_codepoint = 0;

        // Generate bitmaps for the whole character set
        for (uint32_t cp = 32; cp <= 255; cp++) {

            FT_UInt glyph_index = FT_Get_Char_Index(face, cp);
            if (glyph_index > 0) {

				// Add this codepoint to the range, or begin a new range
				if (cp > next_codepoint) rast_font.index.emplace_back<GlyphRange>({ cp, 0 });
				GlyphRange &range = rast_font.index.back();
				range.count++;
				next_codepoint = cp + 1;

				// Repeat for each pixel size  TODO: not just sizes, styles too (bold/italic)
				auto i_var = 0;
				for (auto size : sizes) {

					// Select font size (in pixels)
					fterror = FT_Set_Pixel_Sizes(face, 0, size);
					if (fterror) throw runtime_error("Failed to set character size (in pixels)");

					fterror = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
					if (fterror) throw runtime_error("Failed to get glyph");

					fterror = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
					if (fterror) throw runtime_error("Failed to render loaded glyph");

					FT_GlyphSlot slot = face->glyph;
					FT_Bitmap &bitmap = slot->bitmap;

					assert((slot->advance.x & 0x3f) == 0);

					auto variant = rast_font.variants[i_var];
					auto &pixels = variant.pixels;

					variant.glyphs.emplace_back<RasterizedFont::GlyphRecord>({
						{
							bitmap.width, bitmap.rows,
							slot->bitmap_left, slot->bitmap_top,
							slot->advance.x >> 6, slot->advance.y >> 6
						},
						variant.pixels.size()
					});

					auto &cbox = variant.glyphs.back().cbox;
					cout << "Codepoint: " << cp << ", " << "glyph: " << glyph_index << " " << "at size " << size << ": "
						 << "width: "  << cbox.width << ", " << "height: " << cbox.rows << ", "
						 << "left: "   << cbox.left  << ", " << "top: "    << cbox.top  << ", "
						 << "adv_x : " << cbox.adv_x << ", " << "adv_y: "  << cbox.adv_y 
						 << endl;

					// Copy the pixels
					uint32_t pixel_base = pixels.size();
					pixels.resize(pixel_base + cbox.width * cbox.rows);
					auto dit = pixels.begin() + pixel_base;
					auto sit = bitmap.buffer;
					for (int i = 0; i < bitmap.rows; i++, sit += bitmap.pitch)
						for (int j = 0; j < bitmap.width; j++) *dit++ = sit[j];

					glyph_count++;

					// Next variant (size)
					i_var++;

				} // each pixel size
            }
            else {
                cout << "No glyph for codepoint " << cp << endl;
				missing_count++;
            }
        }

		cout << endl;
		cout << "Total number of glyphs:               " << glyph_count << endl;
		cout << "Number of glyph ranges:               " << rast_font.index.size() << endl;
		cout << "Total number of codepoints not found: " << missing_count << endl;
		cout << "Total number of pixels:               " <<	rast_font.variants.back().pixels.size() << endl; // TODO: multiple variants
    }
    catch(exception &e) {
        cerr << "Error: " << e.what() << endl;
    }

	cout << endl;
    cout << "Press RETURN to terminate" << endl;
    cin.ignore();

    return exit_code;
}