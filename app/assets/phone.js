.pragma library

// The linking page's countries (step 2.3): countries.js's data as objects in the phone's
// language, and the country of an international number. QML hands the data in: a .import of
// countries.js left this file undefined on the Q10 (2026-10-31).

var cache = {}

// Every country of data (countries.js), sorted by its name in language ("it", else English):
// {region, code, prefix, name, flag, main}.
function all(data, language) {
    var lang = language == "it" ? "it" : "en"
    if (!cache[lang]) {
        var list = []
        for (var i = 0; i < data.length; i++) {
            var r = data[i]
            list.push({region: r[0], code: r[1], prefix: r[2], name: lang == "it" ? r[4] : r[3],
                       flag: flag(r[0]), main: r[5] == 1})
        }
        list.sort(function (a, b) { return a.name.localeCompare(b.name) })
        cache[lang] = list
    }
    return cache[lang]
}

function byRegion(list, region) {
    for (var i = 0; i < list.length; i++) {
        if (list[i].region == region)
            return list[i]
    }
    return null
}

// A region's flag: its two letters as regional indicators (U+1F1E6 on), in UTF-16 surrogate
// pairs, since ES5 has no String.fromCodePoint.
function flag(region) {
    return String.fromCharCode(0xD83C, 0xDDE6 + region.charCodeAt(0) - 65,
                               0xD83C, 0xDDE6 + region.charCodeAt(1) - 65)
}

// The country of an international number from its digits after "+" or "00", and the national
// number left; null until the digits hold a whole calling code. Calling codes are prefix-free,
// so the first that matches is the one. Of the regions that share a code (+1, +7, +44), the
// phone's own region wins, then the code's main one.
function detect(list, digits, home) {
    for (var n = 1; n <= 3 && n <= digits.length; n++) {
        var code = parseInt(digits.substr(0, n), 10)
        var best = null
        for (var i = 0; i < list.length; i++) {
            var c = list[i]
            if (c.code != code)
                continue
            if (c.region == home) {
                best = c
                break
            }
            if (c.main || !best)
                best = c
        }
        if (best)
            return {country: best, rest: digits.substr(n)}
    }
    return null
}

// The number for link.code: "+", the calling code, and the national number without its
// national prefix ("07911 123456" in the UK is "+447911123456"; Italy keeps its 0).
function international(country, national) {
    var d = national.replace(/\D/g, "")
    if (country.prefix && d.indexOf(country.prefix) == 0)
        d = d.substr(country.prefix.length)
    return "+" + country.code + d
}
