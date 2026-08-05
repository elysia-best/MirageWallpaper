.pragma library

var translations = null

function translationTable() {
    if (translations !== null) return translations

    translations = ({})
    try {
        var request = new XMLHttpRequest()
        request.open("GET", "qrc:/i18n/ui_zh-chs.json", false)
        request.send()
        if (request.status === 0 || (request.status >= 200 && request.status < 300)) {
            translations = JSON.parse(request.responseText)
        }
    } catch (error) {
        translations = ({})
    }
    return translations
}

function stripMarkup(value) {
    return String(value || "")
        .replace(/＜/g, "<")
        .replace(/＞/g, ">")
        .replace(/<br\s*\/?>/gi, "\n")
        .replace(/<[^>]*>/g, "")
        .replace(/[ \t]+/g, " ")
        .replace(/\n{3,}/g, "\n\n")
        .trim()
}

function displayText(value) {
    var raw = stripMarkup(value)
    if (raw.length === 0) return raw

    var table = translationTable()
    if (table[raw] !== undefined) return stripMarkup(table[raw])
    if (raw.indexOf("ui_") !== 0) return raw

    var prefixes = [
        "ui_editor_script_snippet_",
        "ui_editor_properties_",
        "ui_browse_properties_",
        "ui_editor_general_",
        "ui_editor_effect_",
        "ui_editor_preset_",
        "ui_editor_",
        "ui_browse_",
        "ui_"
    ]
    var readable = raw
    for (var index = 0; index < prefixes.length; ++index) {
        if (readable.indexOf(prefixes[index]) === 0) {
            readable = readable.substring(prefixes[index].length)
            break
        }
    }
    return readable.replace(/_/g, " ").trim()
}

function propertyText(key, text) {
    var raw = String(text || "").trim()
    if (raw.length === 0) raw = String(key || "")
    return displayText(raw)
}
