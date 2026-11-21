.pragma library
// The languages a message or a chat can be translated into (TranslatePicker and TranslateDialog's "Translate to"):
// Google Translate's, as TDLib's translateText lists them (Telegram's service accepts no other), one code each where
// TDLib has aliases (he not iw, id not in, yi not ji, zh-CN and zh-TW not zh, pt not pt-BR). The code, then the
// language's name in its own language (never translated); Latin script first, alphabetical, then the other scripts.
// A function, not a variable: the list stays a plain JS array when it crosses into QML properties.
function list() {
    return [["af", "Afrikaans"], ["az", "Azərbaycan"], ["id", "Bahasa Indonesia"], ["ms", "Bahasa Melayu"],
        ["jv", "Basa Jawa"], ["su", "Basa Sunda"], ["bs", "Bosanski"], ["ca", "Català"], ["ceb", "Cebuano"],
        ["cs", "Čeština"], ["ny", "Chichewa"], ["sn", "ChiShona"], ["co", "Corsu"], ["cy", "Cymraeg"],
        ["da", "Dansk"], ["de", "Deutsch"], ["et", "Eesti"], ["en", "English"], ["es", "Español"],
        ["eo", "Esperanto"], ["eu", "Euskara"], ["tl", "Filipino"], ["fr", "Français"], ["fy", "Frysk"],
        ["ga", "Gaeilge"], ["sm", "Gagana Samoa"], ["gd", "Gàidhlig"], ["gl", "Galego"], ["ha", "Hausa"],
        ["hmn", "Hmoob"], ["hr", "Hrvatski"], ["ig", "Igbo"], ["xh", "isiXhosa"], ["zu", "isiZulu"],
        ["is", "Íslenska"], ["it", "Italiano"], ["rw", "Kinyarwanda"], ["sw", "Kiswahili"],
        ["ht", "Kreyòl ayisyen"], ["ku", "Kurdî"], ["la", "Latina"], ["lv", "Latviešu"],
        ["lb", "Lëtzebuergesch"], ["lt", "Lietuvių"], ["hu", "Magyar"], ["mg", "Malagasy"], ["mt", "Malti"],
        ["mi", "Māori"], ["nl", "Nederlands"], ["no", "Norsk"], ["haw", "ʻŌlelo Hawaiʻi"], ["uz", "Oʻzbek"],
        ["pl", "Polski"], ["pt", "Português"], ["ro", "Română"], ["st", "Sesotho"], ["sq", "Shqip"],
        ["sk", "Slovenčina"], ["sl", "Slovenščina"], ["so", "Soomaali"], ["fi", "Suomi"], ["sv", "Svenska"],
        ["vi", "Tiếng Việt"], ["tr", "Türkçe"], ["tk", "Türkmen"], ["yo", "Yorùbá"],
        ["el", "Ελληνικά"],
        ["be", "Беларуская"], ["bg", "Български"], ["ky", "Кыргызча"], ["kk", "Қазақ тілі"],
        ["mk", "Македонски"], ["mn", "Монгол"], ["ru", "Русский"], ["sr", "Српски"], ["tt", "Татар"],
        ["tg", "Тоҷикӣ"], ["uk", "Українська"],
        ["hy", "Հայերեն"], ["ka", "ქართული"], ["yi", "ייִדיש"], ["he", "עברית"],
        ["ur", "اردو"], ["ar", "العربية"], ["ps", "پښتو"], ["sd", "سنڌي"], ["fa", "فارسی"], ["ug", "ئۇيغۇرچە"],
        ["hi", "हिन्दी"], ["mr", "मराठी"], ["ne", "नेपाली"], ["bn", "বাংলা"], ["pa", "ਪੰਜਾਬੀ"], ["gu", "ગુજરાતી"],
        ["or", "ଓଡ଼ିଆ"], ["ta", "தமிழ்"], ["te", "తెలుగు"], ["kn", "ಕನ್ನಡ"], ["ml", "മലയാളം"], ["si", "සිංහල"],
        ["th", "ไทย"], ["lo", "ລາວ"], ["km", "ខ្មែរ"], ["my", "မြန်မာ"], ["am", "አማርኛ"],
        ["zh-CN", "中文（简体）"], ["zh-TW", "中文（繁體）"], ["ja", "日本語"], ["ko", "한국어"]]
}

// The name list() gives code, "" for a code it hasn't.
function name(code) {
    var all = list()
    for (var i = 0; i < all.length; i++) {
        if (all[i][0] == code) {
            return all[i][1]
        }
    }
    return ""
}
