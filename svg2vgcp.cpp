// svg2vgcp.cpp - SVG to VGCP 3.3 Converter
// Converts SVG files to VGCP protocol commands
// Requires: pugixml library

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <regex>
#define PUGIXML_HEADER_ONLY
#include "pugixml/pugixml.hpp"

// ============================================================================
// Configuration & State Structures
// ============================================================================

struct Config {
    bool indent = false;           // Pretty print output
    bool use_shorthands = true;    // Use abbreviated parameter names (sw, grp, etc.)
    bool flatten_groups = false;   // Flatten group hierarchies
    bool preserve_ids = true;      // Keep original IDs when possible
    std::string default_fill = "none";
    std::string default_stroke = "none";
    double default_stroke_width = 0.0;
};

struct TraversalState {
    std::string parent_grp = "root";
    std::string fill;
    std::string stroke;
    double stroke_width = 0.0;
    double opacity = 1.0;
    std::string transform;
    bool in_symbol = false;
    int indent_level = 0;
};

// ============================================================================
// Helper Functions
// ============================================================================

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

std::string escape_string(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '\n' || c == '\r') {
            result += '\\';
            result += c;
        } else {
            result += c;
        }
    }
    return result;
}

double safe_stod(const std::string& s, double default_val = 0.0) {
    if (s.empty()) return default_val;
    try {
        return std::stod(s);
    } catch (...) {
        return default_val;
    }
}

std::string color_to_hex(const std::string& color_str) {
    std::string color = trim(color_str);
    if (color.empty() || color == "none") return "none";
    
    // Convertir hex corto (#F00 -> #FF0000)
    if (color[0] == '#') {
        if (color.length() == 4) { 
            return std::string("#") + color[1] + color[1] + color[2] + color[2] + color[3] + color[3];
        }
        return color;
    }
    
    // Soporte para funciones rgb() y rgba() de SVG
    if (color.rfind("rgb", 0) == 0) {
        std::regex rgba_regex(R"(rgba?\s*\(\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*(?:,\s*([0-9.]+)\s*)?\))");
        std::smatch match;
        if (std::regex_search(color, match, rgba_regex)) {
            int r = std::stoi(match[1].str());
            int g = std::stoi(match[2].str());
            int b = std::stoi(match[3].str());
            std::ostringstream oss;
            oss << "#" << std::setfill('0') << std::setw(2) << std::hex << r
                << std::setfill('0') << std::setw(2) << std::hex << g
                << std::setfill('0') << std::setw(2) << std::hex << b;
            if (match[4].matched) {
                double a = std::stod(match[4].str());
                int alpha = std::round(a * 255.0);
                oss << std::setfill('0') << std::setw(2) << std::hex << alpha;
            }
            return oss.str();
        }
    }
    
    // Basic named colors
    static const std::map<std::string, std::string> named_colors = {
        {"black", "#000000"}, {"white", "#ffffff"}, {"red", "#ff0000"},
        {"green", "#00ff00"}, {"blue", "#0000ff"}, {"yellow", "#ffff00"},
        {"cyan", "#00ffff"}, {"magenta", "#ff00ff"}, {"gray", "#808080"},
        {"grey", "#808080"}, {"orange", "#ffa500"}, {"purple", "#800080"},
        {"pink", "#ffc0cb"}, {"brown", "#a52a2a"}, {"lime", "#00ff00"},
        {"navy", "#000080"}, {"teal", "#008080"}, {"olive", "#808000"},
        {"maroon", "#800000"}, {"silver", "#c0c0c0"}, {"gold", "#ffd700"}
    };
    
    auto it = named_colors.find(color);
    if (it != named_colors.end()) return it->second;
    return color;
}

std::string format_number(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << value;
    std::string s = oss.str();
    s.erase(s.find_last_not_of('0') + 1, std::string::npos);
    if (s.back() == '.') s.pop_back();
    return s;
}

std::string get_full_text(pugi::xml_node node) {
    std::string result = node.text().get();
    for (pugi::xml_node child : node.children("tspan")) {
        result += child.text().get();
    }
    return trim(result);
}

std::string normalize_preserve_aspect_ratio(const std::string& pr) {
    std::string s = trim(pr);
    if (s.empty()) return "";
    if (s.find("none") != std::string::npos) return "none";
    if (s.find("slice") != std::string::npos) return "slice";
    return "meet";
}

// Convierte la sintaxis de transformaciones SVG a la especificación VGCP 3.3
std::string convert_transform(const std::string& tr) {
    if (tr.empty() || tr == "none") return "";
    std::regex tr_regex(R"(([a-zA-Z]+)\s*\(([^)]*)\))");
    std::sregex_iterator it(tr.begin(), tr.end(), tr_regex);
    std::sregex_iterator end;
    std::string result;
    
    for (; it != end; ++it) {
        std::string name = (*it)[1].str();
        std::string args_str = (*it)[2].str();
        
        if (name == "translate") name = "t";
        else if (name == "rotate") name = "r";
        else if (name == "scale") name = "s";
        
        std::string cleaned_args;
        std::regex arg_split(R"([,\s]+)");
        std::sregex_token_iterator arg_it(args_str.begin(), args_str.end(), arg_split, -1);
        std::sregex_token_iterator arg_end;
        
        for (; arg_it != arg_end; ++arg_it) {
            std::string arg = trim(*arg_it);
            if (!arg.empty()) {
                if (!cleaned_args.empty()) cleaned_args += ",";
                cleaned_args += arg;
            }
        }
        if (cleaned_args.empty() && !trim(args_str).empty()) {
            cleaned_args = trim(args_str);
        }
        
        if (!result.empty()) result += " ";
        result += name + "(" + cleaned_args + ")";
    }
    return result;
}

std::string convert_path_data(const std::string& d) {
    std::string result;
    bool last_was_cmd = false;
    
    for (char c : d) {
        if (c == ',' || c == '\t') {
            result += ' ';
            continue;
        }
        if (c == '-' && !last_was_cmd) {
            result += ' ';
        }
        result += c;
        last_was_cmd = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }
    
    result = std::regex_replace(result, std::regex("\\s+"), " ");
    return trim(result);
}

std::string convert_points(const std::string& points) {
    std::string result = points;
    std::replace(result.begin(), result.end(), ',', ' ');
    result = std::regex_replace(result, std::regex("\\s+"), " ");
    return trim(result);
}

// ============================================================================
// Style Parser
// ============================================================================

struct StyleProperties {
    std::string fill;
    std::string stroke;
    double stroke_width = 0;
    double opacity = 1;
    std::string font_family;
    double font_size = 14;
    std::string font_weight = "normal";
    std::string text_anchor = "start";
};

StyleProperties parse_style(const std::string& style_str) {
    StyleProperties props;
    if (style_str.empty()) return props;
    
    std::regex prop_regex(R"(([a-zA-Z-]+)\s*:\s*([^;]+);?)");
    std::sregex_iterator it(style_str.begin(), style_str.end(), prop_regex);
    std::sregex_iterator end;
    
    for (; it != end; ++it) {
        std::string key = trim((*it)[1].str());
        std::string value = trim((*it)[2].str());
        
        if (key == "fill") props.fill = value;
        else if (key == "stroke") props.stroke = value;
        else if (key == "stroke-width") props.stroke_width = safe_stod(value);
        else if (key == "opacity") props.opacity = safe_stod(value);
        else if (key == "font-family") props.font_family = value;
        else if (key == "font-size") props.font_size = safe_stod(value);
        else if (key == "font-weight") props.font_weight = value;
        else if (key == "text-anchor") props.text_anchor = value;
    }
    
    return props;
}

// ============================================================================
// Element Attributes Container
// ============================================================================

struct Attributes {
    std::string id;
    std::string grp = "root";
    std::string pos;
    std::string class_names;
    std::string fill;
    std::string stroke;
    double stroke_width = 0;
    double opacity = 1;
    std::string transform;
    std::string href;
    std::string preserve_aspect_ratio;
    std::string viewbox;
    
    // Geometry
    double x = 0, y = 0, w = 0, h = 0;
    double cx = 0, cy = 0, r = 0;
    double rx = 0, ry = 0;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::string d;
    std::string points;
    std::string content;
    double font_size = 14;
    std::string font_family = "Helvetica";
    std::string font_weight = "normal";
    std::string text_anchor = "start";
    
    std::string to_vgcp(const Config& cfg, const std::string& cmd) const {
        std::ostringstream oss;
        oss << cmd << " id=" << id;
        
        std::string sw_key = cfg.use_shorthands ? "sw" : "stroke-width";
        std::string tr_key = cfg.use_shorthands ? "tr" : "transform";
        std::string op_key = cfg.use_shorthands ? "op" : "opacity";
        std::string grp_key = "grp";
        std::string fs_key = cfg.use_shorthands ? "fs" : "font-size";
        std::string ff_key = cfg.use_shorthands ? "ff" : "font-family";
        std::string fw_key = cfg.use_shorthands ? "fw" : "font-weight";
        std::string ta_key = cfg.use_shorthands ? "ta" : "text-anchor";
        std::string pr_key = cfg.use_shorthands ? "pr" : "preserveAspectRatio";
        
        if (!grp.empty() && grp != "root") oss << " " << grp_key << "=" << grp;
        if (!pos.empty()) oss << " pos=" << pos;
        if (!class_names.empty()) oss << " class=\"" << class_names << "\"";
        
        if (!fill.empty()) oss << " fill=" << color_to_hex(fill);
        if (!stroke.empty() && stroke != "none") {
            oss << " stroke=" << color_to_hex(stroke);
            double effective_sw = (stroke_width > 0) ? stroke_width : 1.0;
            oss << " " << sw_key << "=" << format_number(effective_sw);
        }
        if (opacity < 1.0) oss << " " << op_key << "=" << format_number(opacity);
        if (!transform.empty()) oss << " " << tr_key << "=\"" << escape_string(transform) << "\"";
        
        if (cmd == "RCT") {
            if (x != 0) oss << " x=" << format_number(x);
            if (y != 0) oss << " y=" << format_number(y);
            if (w > 0) oss << " w=" << format_number(w);
            if (h > 0) oss << " h=" << format_number(h);
            if (rx > 0) oss << " rx=" << format_number(rx);
            if (ry > 0) oss << " ry=" << format_number(ry);
        }
        else if (cmd == "CIR") {
            oss << " cx=" << format_number(cx) << " cy=" << format_number(cy);
            oss << " r=" << format_number(r);
        }
        else if (cmd == "ELL") {
            oss << " cx=" << format_number(cx) << " cy=" << format_number(cy);
            oss << " rx=" << format_number(rx) << " ry=" << format_number(ry);
        }
        else if (cmd == "LIN") {
            oss << " x1=" << format_number(x1) << " y1=" << format_number(y1);
            oss << " x2=" << format_number(x2) << " y2=" << format_number(y2);
        }
        else if (cmd == "PAT") {
            oss << " d=\"" << escape_string(d) << "\"";
        }
        else if (cmd == "PGN" || cmd == "PLN") {
            oss << " pts=\"" << points << "\"";
        }
        else if (cmd == "TXT") {
            if (x != 0) oss << " x=" << format_number(x);
            if (y != 0) oss << " y=" << format_number(y);
            oss << " txt=\"" << escape_string(content) << "\"";
            if (font_size != 14) oss << " " << fs_key << "=" << format_number(font_size);
            if (!font_family.empty() && font_family != "Helvetica") {
                oss << " " << ff_key << "=" << font_family;
            }
            if (font_weight == "bold" || font_weight == "700") oss << " " << fw_key << "=b";
            if (text_anchor != "start") {
                std::string ta = (text_anchor == "middle") ? "m" : ((text_anchor == "end") ? "e" : "s");
                oss << " " << ta_key << "=" << ta;
            }
        }
        else if (cmd == "IMG") {
            if (x != 0) oss << " x=" << format_number(x);
            if (y != 0) oss << " y=" << format_number(y);
            if (w > 0) oss << " w=" << format_number(w);
            if (h > 0) oss << " h=" << format_number(h);
            oss << " href=\"" << href << "\"";
            std::string pr_val = normalize_preserve_aspect_ratio(preserve_aspect_ratio);
            if (!pr_val.empty() && pr_val != "meet") {
                oss << " " << pr_key << "=" << pr_val;
            }
        }
        else if (cmd == "SYM") {
            if (!viewbox.empty()) oss << " vb=\"" << viewbox << "\"";
            std::string pr_val = normalize_preserve_aspect_ratio(preserve_aspect_ratio);
            if (!pr_val.empty() && pr_val != "meet") {
                oss << " " << pr_key << "=" << pr_val;
            }
        }
        else if (cmd == "USE") {
            oss << " sym=" << href;
            if (x != 0) oss << " x=" << format_number(x);
            if (y != 0) oss << " y=" << format_number(y);
            if (w > 0) oss << " w=" << format_number(w);
            if (h > 0) oss << " h=" << format_number(h);
        }
        
        return oss.str();
    }
};

// ============================================================================
// SVG to VGCP Converter Core Engine
// ============================================================================

class SVG2VGCPConverter {
public:
    SVG2VGCPConverter(const Config& cfg) : config(cfg) {}
    
    bool load(const std::string& filename) {
        pugi::xml_parse_result result = doc.load_file(filename.c_str());
        if (!result) {
            std::cerr << "Error loading SVG: " << result.description() << std::endl;
            return false;
        }
        return true;
    }
    
    std::string convert() {
        std::ostringstream output;
        pugi::xml_node svg = doc.child("svg");
        if (!svg) {
            std::cerr << "No <svg> root element found" << std::endl;
            return "";
        }
        
        double width = 800, height = 600;
        if (svg.attribute("width")) width = svg.attribute("width").as_double(800.0);
        if (svg.attribute("height")) height = svg.attribute("height").as_double(600.0);
        if (svg.attribute("viewBox")) {
            std::string vb = svg.attribute("viewBox").value();
            std::replace(vb.begin(), vb.end(), ',', ' ');
            std::istringstream iss(vb);
            double x, y, w, h;
            if (iss >> x >> y >> w >> h) {
                width = w;
                height = h;
            }
        }
        
        // Detect background color: first look for a full-size <rect> with no id as background,
        // then check SVG style/background-color, otherwise default to white (SVG viewer default).
        std::string bg = "#ffffff";
        for (pugi::xml_node child : svg.children("rect")) {
            double rx = child.attribute("x").as_double(0);
            double ry = child.attribute("y").as_double(0);
            double rw = child.attribute("width").as_double(0);
            double rh = child.attribute("height").as_double(0);
            std::string rfill = child.attribute("fill").value();
            if (rfill.empty()) {
                StyleProperties sp = parse_style(child.attribute("style").value());
                rfill = sp.fill;
            }
            if (!rfill.empty() && rfill != "none" &&
                rx <= 0 && ry <= 0 && rw >= width * 0.9 && rh >= height * 0.9) {
                bg = color_to_hex(rfill);
                break;
            }
        }
        output << "VIEW w=" << format_number(width) << " h=" << format_number(height)
               << " bg=" << bg << "\n";
        
        convert_styles(svg, output);
        convert_defs(svg, output);
        
        element_counter = 0;
        TraversalState initial_state;
        for (pugi::xml_node child : svg.children()) {
            convert_element(child, output, initial_state);
        }
        
        return output.str();
    }
    
private:
    Config config;
    pugi::xml_document doc;
    int element_counter = 0;
    std::map<std::string, std::string> style_map;
    
    std::string generate_id(const std::string& original) {
        if (config.preserve_ids && !original.empty()) {
            return original;
        }
        return "e" + std::to_string(++element_counter);
    }
    
    void print_indent(std::ostringstream& out, int level) {
        if (config.indent && level > 0) {
            out << std::string(level * 2, ' ');
        }
    }
    
    void convert_styles(pugi::xml_node svg, std::ostringstream& out) {
        pugi::xml_node style = svg.child("style");
        if (!style) return;
        
        std::string css = style.text().get();
        if (css.empty()) return;
        
        std::regex rule_regex(R"(([.#]?[a-zA-Z0-9_-]+)\s*\{([^}]+)\})");
        std::sregex_iterator it(css.begin(), css.end(), rule_regex);
        std::sregex_iterator end;
        
        std::string sw_key = config.use_shorthands ? "sw" : "stroke-width";
        std::string op_key = config.use_shorthands ? "op" : "opacity";
        std::string fs_key = config.use_shorthands ? "fs" : "font-size";
        std::string ff_key = config.use_shorthands ? "ff" : "font-family";
        std::string fw_key = config.use_shorthands ? "fw" : "font-weight";
        std::string ta_key = config.use_shorthands ? "ta" : "text-anchor";
        
        for (; it != end; ++it) {
            std::string selector = (*it)[1].str();
            std::string decl = (*it)[2].str();
            
            if (selector[0] == '.') selector = "." + selector.substr(1);
            else if (selector[0] == '#') selector = "#" + selector.substr(1);
            
            std::regex prop_regex(R"(([a-zA-Z-]+)\s*:\s*([^;]+);?)");
            std::sregex_iterator pit(decl.begin(), decl.end(), prop_regex);
            
            std::ostringstream props;
            for (; pit != std::sregex_iterator(); ++pit) {
                std::string prop = (*pit)[1].str();
                std::string value = trim((*pit)[2].str());
                
                if (prop == "fill") props << " fill=" << color_to_hex(value);
                else if (prop == "stroke") props << " stroke=" << color_to_hex(value);
                else if (prop == "stroke-width") props << " " << sw_key << "=" << value;
                else if (prop == "opacity") props << " " << op_key << "=" << value;
                else if (prop == "font-size") props << " " << fs_key << "=" << value;
                else if (prop == "font-family") props << " " << ff_key << "=" << value;
                else if (prop == "font-weight") props << " " << fw_key << "=" << (value == "bold" ? "b" : "n");
                else if (prop == "text-anchor") {
                    std::string ta = (value == "middle" ? "m" : (value == "end" ? "e" : "s"));
                    props << " " << ta_key << "=" << ta;
                }
            }
            
            std::string rule = "STY sel=" + selector + props.str() + "\n";
            style_map[selector] = rule;
            print_indent(out, 0);
            out << rule;
        }
    }
    
    void convert_defs(pugi::xml_node svg, std::ostringstream& out) {
        pugi::xml_node defs = svg.child("defs");
        if (!defs) return;
        
        for (pugi::xml_node sym : defs.children("symbol")) {
            Attributes attr;
            attr.id = generate_id(sym.attribute("id").value());
            
            if (sym.attribute("viewBox")) attr.viewbox = sym.attribute("viewBox").value();
            if (sym.attribute("preserveAspectRatio")) attr.preserve_aspect_ratio = sym.attribute("preserveAspectRatio").value();
            
            print_indent(out, 0);
            out << attr.to_vgcp(config, "SYM") << " begin\n";
            
            TraversalState symbol_state;
            symbol_state.in_symbol = true;
            symbol_state.indent_level = 1;
            
            for (pugi::xml_node child : sym.children()) {
                convert_element(child, out, symbol_state);
            }
            
            print_indent(out, 0);
            out << "SYM end\n";
        }
    }
    
    void convert_element(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        std::string tag = node.name();
        if (tag == "g") convert_group(node, out, state);
        else if (tag == "rect") convert_rect(node, out, state);
        else if (tag == "circle") convert_circle(node, out, state);
        else if (tag == "ellipse") convert_ellipse(node, out, state);
        else if (tag == "line") convert_line(node, out, state);
        else if (tag == "path") convert_path(node, out, state);
        else if (tag == "polygon") convert_polygon(node, out, state);
        else if (tag == "polyline") convert_polyline(node, out, state);
        else if (tag == "text") convert_text(node, out, state);
        else if (tag == "image") convert_image(node, out, state);
        else if (tag == "use") convert_use(node, out, state);
    }
    
    Attributes extract_common_attrs(pugi::xml_node node, const TraversalState& state) {
        Attributes attr;
        attr.id = generate_id(node.attribute("id").value());
        attr.grp = state.in_symbol ? "" : state.parent_grp;
        attr.class_names = node.attribute("class").value();
        
        StyleProperties style;
        if (node.attribute("style")) {
            style = parse_style(node.attribute("style").value());
        }
        
        // Procesar e heredar Fill
        if (!style.fill.empty()) attr.fill = style.fill;
        else if (node.attribute("fill")) attr.fill = node.attribute("fill").value();
        else if (config.flatten_groups && !state.fill.empty()) attr.fill = state.fill;
        
        // Procesar e heredar Stroke
        if (!style.stroke.empty()) attr.stroke = style.stroke;
        else if (node.attribute("stroke")) attr.stroke = node.attribute("stroke").value();
        else if (config.flatten_groups && !state.stroke.empty()) attr.stroke = state.stroke;
        
        // Procesar e heredar Stroke-Width
        if (style.stroke_width > 0) attr.stroke_width = style.stroke_width;
        else if (node.attribute("stroke-width")) attr.stroke_width = node.attribute("stroke-width").as_double();
        else if (config.flatten_groups && state.stroke_width > 0) attr.stroke_width = state.stroke_width;
        
        // Opacidad acumulativa
        double local_opacity = 1.0;
        if (style.opacity < 1.0) local_opacity = style.opacity;
        else if (node.attribute("opacity")) local_opacity = node.attribute("opacity").as_double(1.0);
        
        attr.opacity = config.flatten_groups ? (state.opacity * local_opacity) : local_opacity;
        
        // Composición de transformaciones si se aplana jerarquía
        std::string local_tr = convert_transform(node.attribute("transform").value());
        if (config.flatten_groups) {
            if (!state.transform.empty() && !local_tr.empty()) attr.transform = state.transform + " " + local_tr;
            else if (!state.transform.empty()) attr.transform = state.transform;
            else attr.transform = local_tr;
        } else {
            attr.transform = local_tr;
        }
        
        // Tipografía
        if (style.font_size != 14) attr.font_size = style.font_size;
        else if (node.attribute("font-size")) attr.font_size = node.attribute("font-size").as_double(14.0);
        
        if (!style.font_family.empty()) attr.font_family = style.font_family;
        else if (node.attribute("font-family")) attr.font_family = node.attribute("font-family").value();
        
        if (!style.font_weight.empty()) attr.font_weight = style.font_weight;
        else if (node.attribute("font-weight")) attr.font_weight = node.attribute("font-weight").value();
        
        if (!style.text_anchor.empty()) attr.text_anchor = style.text_anchor;
        else if (node.attribute("text-anchor")) attr.text_anchor = node.attribute("text-anchor").value();
        
        return attr;
    }
    
    void convert_group(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        TraversalState next_state = state;
        
        if (config.flatten_groups) {
            next_state.fill = attr.fill;
            next_state.stroke = attr.stroke;
            next_state.stroke_width = attr.stroke_width;
            next_state.opacity = attr.opacity;
            next_state.transform = attr.transform;
            
            for (pugi::xml_node child : node.children()) {
                convert_element(child, out, next_state);
            }
        } else {
            print_indent(out, state.indent_level);
            out << attr.to_vgcp(config, "GRP") << "\n";
            next_state.parent_grp = attr.id;
            next_state.indent_level = state.indent_level + 1;
            for (pugi::xml_node child : node.children()) {
                convert_element(child, out, next_state);
            }
        }
    }
    
    void convert_rect(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        attr.x = node.attribute("x").as_double(0.0);
        attr.y = node.attribute("y").as_double(0.0);
        attr.w = node.attribute("width").as_double(0.0);
        attr.h = node.attribute("height").as_double(0.0);
        attr.rx = node.attribute("rx").as_double(0.0);
        attr.ry = node.attribute("ry").as_double(0.0);
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "RCT") << "\n";
    }
    
    void convert_circle(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        attr.cx = node.attribute("cx").as_double(0.0);
        attr.cy = node.attribute("cy").as_double(0.0);
        attr.r = node.attribute("r").as_double(0.0);
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "CIR") << "\n";
    }
    
    void convert_ellipse(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        attr.cx = node.attribute("cx").as_double(0.0);
        attr.cy = node.attribute("cy").as_double(0.0);
        attr.rx = node.attribute("rx").as_double(0.0);
        attr.ry = node.attribute("ry").as_double(0.0);
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "ELL") << "\n";
    }
    
    void convert_line(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        attr.x1 = node.attribute("x1").as_double(0.0);
        attr.y1 = node.attribute("y1").as_double(0.0);
        attr.x2 = node.attribute("x2").as_double(0.0);
        attr.y2 = node.attribute("y2").as_double(0.0);
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "LIN") << "\n";
    }
    
    void convert_path(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        if (node.attribute("d")) attr.d = convert_path_data(node.attribute("d").value());
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "PAT") << "\n";
    }
    
    void convert_polygon(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        if (node.attribute("points")) attr.points = convert_points(node.attribute("points").value());
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "PGN") << "\n";
    }
    
    void convert_polyline(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        if (node.attribute("points")) attr.points = convert_points(node.attribute("points").value());
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "PLN") << "\n";
    }
    
    void convert_text(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        attr.x = node.attribute("x").as_double(0.0);
        attr.y = node.attribute("y").as_double(0.0);
        attr.content = get_full_text(node);
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "TXT") << "\n";
    }
    
    void convert_image(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        attr.x = node.attribute("x").as_double(0.0);
        attr.y = node.attribute("y").as_double(0.0);
        attr.w = node.attribute("width").as_double(0.0);
        attr.h = node.attribute("height").as_double(0.0);
        
        std::string href = node.attribute("href").value();
        if (href.empty()) href = node.attribute("xlink:href").value();
        attr.href = href;
        attr.preserve_aspect_ratio = node.attribute("preserveAspectRatio").value();
        
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "IMG") << "\n";
    }
    
    void convert_use(pugi::xml_node node, std::ostringstream& out, const TraversalState& state) {
        Attributes attr = extract_common_attrs(node, state);
        std::string href = node.attribute("href").value();
        if (href.empty()) href = node.attribute("xlink:href").value();
        if (!href.empty() && href[0] == '#') href = href.substr(1);
        attr.href = href; // Reutilizado internamente para mapear el ID del símbolo instanciado
        
        attr.x = node.attribute("x").as_double(0.0);
        attr.y = node.attribute("y").as_double(0.0);
        attr.w = node.attribute("width").as_double(0.0);
        attr.h = node.attribute("height").as_double(0.0);
        
        print_indent(out, state.indent_level);
        out << attr.to_vgcp(config, "USE") << "\n";
    }
};

// ============================================================================
// Main Program Entrypoint
// ============================================================================

void print_usage(const char* progname) {
    std::cout << "Usage: " << progname << " [options] input.svg [output.vgcp]\n"
              << "Options:\n"
              << "  --no-shorthands   Use full attribute names (stroke-width instead of sw)\n"
              << "  --flatten         Flatten group hierarchies and propagate downstream\n"
              << "  --no-preserve-ids Generate new structural IDs instead of keeping originals\n"
              << "  --indent          Pretty print output using semantic indents\n"
              << "  --help            Show this help menu\n";
}

int main(int argc, char* argv[]) {
    Config config;
    std::string input_file;
    std::string output_file;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--no-shorthands") {
            config.use_shorthands = false;
        } else if (arg == "--flatten") {
            config.flatten_groups = true;
        } else if (arg == "--no-preserve-ids") {
            config.preserve_ids = false;
        } else if (arg == "--indent") {
            config.indent = true;
        } else if (input_file.empty()) {
            input_file = arg;
        } else {
            output_file = arg;
        }
    }
    
    if (input_file.empty()) {
        std::cerr << "Error: No input file specified.\n";
        print_usage(argv[0]);
        return 1;
    }
    
    SVG2VGCPConverter converter(config);
    if (!converter.load(input_file)) {
        return 1;
    }
    
    std::string output = converter.convert();
    if (output.empty()) return 1;
    
    if (output_file.empty()) {
        std::cout << output;
    } else {
        std::ofstream ofs(output_file);
        if (!ofs) {
            std::cerr << "Error: Cannot write to output target file: " << output_file << std::endl;
            return 1;
        }
        ofs << output;
        std::cout << "Successfully converted: " << input_file << " -> " << output_file << std::endl;
    }
    
    return 0;
}