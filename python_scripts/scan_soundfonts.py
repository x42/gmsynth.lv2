# ------------------------------------------------------------------------------
# libraries
# ------------------------------------------------------------------------------

import os
import re

# ------------------------------------------------------------------------------
# types
# ------------------------------------------------------------------------------

class Plugin:
    folder: str
    name: str
    extension: str
    version: str

# ------------------------------------------------------------------------------
# constants
# ------------------------------------------------------------------------------

PLUGIN_NAME = 'gmsynth'

MANIFEST_TTL_HEADER = (
    f'@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .{os.linesep}'
    f'@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .{os.linesep}'
    f'@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .{os.linesep}'
)

MANIFEST_TTL_MODULE = (
    f'<http://gareus.org/oss/lv2/@LV2NAME@#@SOUNDFONT@>{os.linesep}'
    f'	a lv2:Plugin ;{os.linesep}'
    f'	lv2:binary <@LV2NAME@@LIB_EXT@>  ;{os.linesep}'
    f'	rdfs:seeAlso <@LV2NAME@.ttl> .{os.linesep}'
)

GMSYNTH_TTL_HEADER = (
    f'@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .{os.linesep}'
    f'@prefix doap:  <http://usefulinc.com/ns/doap#> .{os.linesep}'
    f'@prefix foaf:  <http://xmlns.com/foaf/0.1/> .{os.linesep}'
    f'@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .{os.linesep}'
    f'@prefix midi:  <http://lv2plug.in/ns/ext/midi#> .{os.linesep}'
    f'@prefix pprop: <http://lv2plug.in/ns/ext/port-props#> .{os.linesep}'
    f'@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .{os.linesep}'
    f'@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .{os.linesep}'
    f'@prefix state: <http://lv2plug.in/ns/ext/state#> .{os.linesep}'
    f'@prefix ui:    <http://lv2plug.in/ns/extensions/ui#> .{os.linesep}'
    f'@prefix urid:  <http://lv2plug.in/ns/ext/urid#> .{os.linesep}'
    f'{os.linesep}'
    f'<http://ardour.org/lv2/midnam#interface> a lv2:ExtensionData .{os.linesep}'
    f'<http://ardour.org/lv2/midnam#update> a lv2:Feature .{os.linesep}'
    f'<http://ardour.org/lv2/bankpatch#notify> a lv2:Feature .{os.linesep}'
    f'{os.linesep}'
    f'<http://gareus.org/rgareus#me>{os.linesep}'
    f'	a foaf:Person ;{os.linesep}'
    f'	foaf:name "Robin Gareus" ;{os.linesep}'
    f'	foaf:mbox <mailto:robin@gareus.org> ;{os.linesep}'
    f'	foaf:homepage <http://gareus.org/> .{os.linesep}'
)

GMSYNTH_TTL_MODULE = (
    f'<http://gareus.org/oss/lv2/@LV2NAME@#@SOUNDFONT@> {os.linesep}'
    f'  a doap:Project, lv2:InstrumentPlugin, lv2:Plugin ; {os.linesep}'
    f'  doap:name "gmsynth @SOUNDFONT@" ; {os.linesep}'
    f'  doap:maintainer <http://gareus.org/rgareus#me>; {os.linesep}'
    f'  doap:license <http://usefulinc.com/doap/licenses/gpl> ; {os.linesep}'
    f'  @VERSION@ {os.linesep}'
    f'  lv2:requiredFeature urid:map; {os.linesep}'
    f'  lv2:optionalFeature lv2:hardRTCapable; {os.linesep}'
    f'	lv2:optionalFeature <http://ardour.org/lv2/midnam#update>; {os.linesep}'
    f'	lv2:optionalFeature <http://ardour.org/lv2/bankpatch#notify>; {os.linesep}'
    f'	lv2:extensionData <http://ardour.org/lv2/midnam#interface>; {os.linesep}'
    f'  lv2:port [ {os.linesep}'
    f'      a lv2:InputPort, atom:AtomPort ; {os.linesep}'
    f'      atom:bufferType atom:Sequence ; {os.linesep}'
    f'      atom:supports midi:MidiEvent; {os.linesep}'
    f'      lv2:designation lv2:control ; {os.linesep}'
    f'      lv2:index 0 ; {os.linesep}'
    f'      lv2:symbol "control" ; {os.linesep}'
    f'      lv2:name "Midi In" ; {os.linesep}'
    f'  ] , [ {os.linesep}'
    f'      a lv2:OutputPort, lv2:AudioPort ; {os.linesep}'
    f'      lv2:index 1 ; {os.linesep}'
    f'      lv2:symbol "outL" ; {os.linesep}'
    f'      lv2:name "Output Left" ; {os.linesep}'
    f'  ] , [ {os.linesep}'
    f'      a lv2:OutputPort, lv2:AudioPort ; {os.linesep}'
    f'      lv2:index 2 ; {os.linesep}'
    f'      lv2:symbol "outR" ; {os.linesep}'
    f'      lv2:name "Output Right" ; {os.linesep}'
    f'  ] . {os.linesep}'
)

# ------------------------------------------------------------------------------
# main function
# ------------------------------------------------------------------------------

def main() -> None:

    plugin = Plugin()
    plugin.folder = get_plugin_bundle_path()
    plugin.name = PLUGIN_NAME
    plugin.extension = get_plugin_extension(plugin.folder)
    plugin.version = get_plugin_version(plugin.folder)

    sf2_folder: str = os.path.join(plugin.folder, 'soundfonts')
    sanitize_sf2_filenames(sf2_folder)
    sf2_list: list[str] = get_sf2_list(sf2_folder)

    manifest_content: str = compile_template(MANIFEST_TTL_HEADER, plugin)
    manifest_content += os.linesep
    
    gmsynth_content: str = compile_template(GMSYNTH_TTL_HEADER, plugin)
    gmsynth_content += os.linesep
    
    soundfonts_list: str = ''

    manifest_module = compile_template(MANIFEST_TTL_MODULE, plugin)
    gmsynth_module = compile_template(GMSYNTH_TTL_MODULE, plugin)

    for sf2_file in sf2_list:

        manifest_content += manifest_module.replace('@SOUNDFONT@', sf2_file)
        manifest_content += os.linesep
        
        gmsynth_content += gmsynth_module.replace('@SOUNDFONT@', sf2_file)
        gmsynth_content += os.linesep
        
        soundfonts_list += sf2_file
        soundfonts_list += os.linesep

    write_to_file(os.path.join(plugin.folder, 'manifest.ttl'), manifest_content)
    write_to_file(os.path.join(plugin.folder, f'{PLUGIN_NAME}.ttl'), gmsynth_content)
    write_to_file(os.path.join(plugin.folder, 'soundfonts_list.txt'), soundfonts_list)

    console_log('Finished.')

# ------------------------------------------------------------------------------
# auxiliary functions
# ------------------------------------------------------------------------------

def console_log(data: str) -> None:
    print(data)

def get_plugin_bundle_path() -> str:
    this_script = os.path.realpath(__file__)
    bundle_path = os.path.dirname(this_script)
    console_log(f'Plugin path is "{bundle_path}"')
    return bundle_path

def get_plugin_extension(folder_path: str) -> str:
    extension = ''
    files_list: list[str] = os.listdir(folder_path)
    for file in files_list:
        if file.startswith(PLUGIN_NAME) and not file.endswith('.ttl'):
            extension = file.replace(PLUGIN_NAME, '')
            break
    console_log(f'Plugin extension is "{extension}"')
    return extension

def get_plugin_version(folder_path: str) -> str:
    version = ''
    with open(os.path.join(folder_path, f'{PLUGIN_NAME}.ttl')) as file:
        for line in file:
            data = line.strip()
            if 'lv2:microVersion' in data or 'lv2:minorVersion' in data:
                version = data
                break
    console_log(f'Plugin version is "{version}"')
    return version

def get_sf2_list(folder_path: str) -> list[str]:
    files_list: list[str] = os.listdir(folder_path)
    sf2_list: list[str] = []
    for file in files_list:
        if file.endswith('.sf2'):
            console_log(f'Found soundfont "{file}"')
            sf2_list.append(file)
    return sf2_list

def compile_template(template: str, plug_data: Plugin) -> str:
    out = template
    out = out.replace('@LV2NAME@', plug_data.name)
    out = out.replace('@LIB_EXT@', plug_data.extension)
    out = out.replace('@VERSION@', plug_data.version)
    return out

def write_to_file(path: str, data: str) -> None:
    with open(path, "wb") as file:
        file.write(data.encode("utf-8"))
    console_log(f'Written file {path}')

def sanitize_sf2_filenames(path: str) -> None:
    valid_chars = re.compile(r"[^A-Za-z0-9\-._~:/?#\[\]@!$&'()*+,;=]")
    for root, _, files in os.walk(path):
        for name in files:
            if name.endswith('.sf2'):
                sanitized = valid_chars.sub('_', name)
                if sanitized != name:
                    old_path = os.path.join(root, name)
                    new_path = os.path.join(root, sanitized)
                    os.rename(old_path, new_path)
                    console_log(f'Sanitized "{name}" into "{sanitized}"')

# ------------------------------------------------------------------------------
# runner
# ------------------------------------------------------------------------------

main()