const CONFIG_URL = "https://testarossa47.github.io/jw-daily-text/config/";
const DEFAULT_LANG = { locale: "en", lib: "lp-e", name: "English" };

function stripTags(text) {
	return text.replace(/<[^>]+>/g, "").replace(/\s+/g, " ").replace(/\u200b/g, "").trim();
}

function getStoredLanguage() {
	try {
		const stored = localStorage.getItem("dt_language");
		if (stored) return JSON.parse(stored);
	} catch (e) {}
	return DEFAULT_LANG;
}

function fetchDailyTextFromWol(dateStr, lang, callback) {
	const parts = dateStr.split("-");
	const url = `https://wol.jw.org/${lang.locale}/wol/dt/r1/${lang.lib}/${parseInt(parts[0])}/${parseInt(parts[1])}/${parseInt(parts[2])}`;

	const req = new XMLHttpRequest();
	req.open("GET", url, true);
	req.timeout = 15000;
	req.onload = function () {
		if (req.status !== 200) {
			callback({ error: `HTTP ${req.status}` });
			return;
		}
		const html = req.responseText;
		const themeMatch = html.match(/<p[^>]*class="themeScrp"[^>]*>([\s\S]*?)<\/p>/);
		if (!themeMatch) {
			callback({ error: "No themeScrp found" });
			return;
		}
		const themeHtml = themeMatch[1];
		const refMatch = themeHtml.match(/<a[^>]*>([\s\S]*?)<\/a>/);
		const ref = refMatch ? stripTags(refMatch[1]) : "";

		const emMatches = themeHtml.match(/<em>([\s\S]*?)<\/em>/g) || [];
		const textParts = [];
		for (const em of emMatches) {
			const inner = em.replace(/<\/?em>/g, "").replace(/<a[^>]*>[\s\S]*?<\/a>/, "").trim();
			const cleaned = stripTags(inner);
			if (cleaned && !cleaned.includes(ref.replace(/\s+/g, " ").trim())) {
				textParts.push(cleaned);
			}
		}
		const text = textParts.join(" ").replace(/,+$/, "").trim();

		const bodyMatch = html.match(/<div class="bodyTxt">([\s\S]*?)<\/div>/);
		let commentary = "";
		if (bodyMatch) {
			const pMatch = bodyMatch[1].match(/<p[^>]*class="[^"]*p\d+[^"]*"[^>]*>([\s\S]*?)<\/p>/);
			if (pMatch) {
				commentary = stripTags(pMatch[1]);
				const dash = commentary.lastIndexOf("\u2014");
				if (dash > 0) commentary = commentary.substring(0, dash).trim();
				commentary = commentary.replace(/\s*\.\s*$/, "");
			}
		}
		callback({ date: dateStr, ref: ref, text: text, commentary: commentary });
	};
	req.onerror = function () { callback({ error: "Network error" }); };
	req.ontimeout = function () { callback({ error: "Timeout" }); };
	req.send();
}

Pebble.addEventListener("ready", function () {
	console.log("JW Daily Text phone proxy ready");
});

Pebble.addEventListener("showConfiguration", function () {
	const lang = getStoredLanguage();
	const url = CONFIG_URL + "?locale=" + encodeURIComponent(lang.locale) + "&lib=" + encodeURIComponent(lang.lib);
	console.log("Opening config: " + url);
	Pebble.openURL(url);
});

Pebble.addEventListener("webviewclosed", function (e) {
	try {
		const config = JSON.parse(decodeURIComponent(e.response));
		if (config && config.locale && config.lib) {
			localStorage.setItem("dt_language", JSON.stringify(config));
			console.log("Language set: " + config.name + " (" + config.locale + ")");
			Pebble.sendAppMessage({
				action: "language_changed",
				language: config.locale
			});
		}
	} catch (err) {
		console.log("Config error: " + err);
	}
});

Pebble.addEventListener("appmessage", function (e) {
	const payload = e.payload;
	const lang = getStoredLanguage();

	if (payload.action === "fetch") {
		fetchDailyTextFromWol(payload.date, lang, function (result) {
			if (result.error) {
				Pebble.sendAppMessage({
					action: "fetch_error",
					date: payload.date,
					error: result.error
				});
			} else {
				Pebble.sendAppMessage({
					action: "fetch_result",
					date: result.date,
					ref: result.ref,
					text: result.text,
					commentary: result.commentary
				});
			}
		});
		return;
	}

	if (payload.action === "open_config") {
		Pebble.showConfiguration();
	}
});
