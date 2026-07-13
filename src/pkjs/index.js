const CONFIG_URL = "https://testarossa47.github.io/jw-daily-text/config/";

const ACTION_FETCH = 1;
const ACTION_FETCH_RESULT = 2;
const ACTION_FETCH_ERROR = 3;
const ACTION_LANGUAGE_CHANGED = 4;

let currentLocale = "en";
let currentLib = "lp-e";
let currentRsconf = "1";

function stripTags(text) {
	return text.replace(/<[^>]+>/g, "").replace(/\s+/g, " ").replace(/\u200b/g, "").trim();
}

function daysInMonth(y, m) {
	return new Date(y, m, 0).getDate();
}

function fetchDailyTextFromWol(dateStr, locale, lib, rsconf, callback) {
	const parts = dateStr.split("-");
	const url = `https://wol.jw.org/${locale}/wol/dt/r${rsconf}/${lib}/${parseInt(parts[0])}/${parseInt(parts[1])}/${parseInt(parts[2])}`;

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
			const pMatch = bodyMatch[1].match(/<p[^>]*>([\s\S]*?)<\/p>/);
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

function sendResult(date, locale, lib, rsconf, ref, text, commentary) {
	Pebble.sendAppMessage({
		action: ACTION_FETCH_RESULT,
		date: date,
		ref: ref,
		text: text,
		commentary: commentary,
		language: locale,
		lib: lib,
		rsconf: rsconf
	});
}

function preFetchDays(year, month, startDay, endDay, locale, lib, rsconf) {
	if (startDay > endDay) return;
	const dateStr = year + "-" + ("0" + month).slice(-2) + "-" + ("0" + startDay).toString().slice(-2);
	fetchDailyTextFromWol(dateStr, locale, lib, rsconf, function (result) {
		if (!result.error) {
			sendResult(result.date, locale, lib, rsconf, result.ref, result.text, result.commentary);
		}
		const next = startDay + 1;
		if (next <= endDay) {
			setTimeout(function () {
				preFetchDays(year, month, next, endDay, locale, lib, rsconf);
			}, 300);
		}
	});
}

Pebble.addEventListener("ready", function () {
	console.log("JW Daily Text phone proxy ready");
});

Pebble.addEventListener("showConfiguration", function () {
	Pebble.openURL(CONFIG_URL + "?v=" + Date.now() + "&locale=" + currentLocale + "&lib=" + currentLib + "&rsconf=" + currentRsconf);
});

Pebble.addEventListener("webviewclosed", function (e) {
	if (!e.response) return;
	try {
		const config = JSON.parse(decodeURIComponent(e.response));
		if (config && config.locale && config.lib) {
			currentLocale = config.locale;
			currentLib = config.lib;
			currentRsconf = config.rsconf || "1";
			Pebble.sendAppMessage({
				action: ACTION_LANGUAGE_CHANGED,
				language: config.locale,
				lib: config.lib,
				rsconf: config.rsconf || "1",
				cache_days: config.cacheDays || 7
			});
			console.log("Language set: " + config.name + " (" + config.locale + ", " + config.lib + ", r" + config.rsconf + ")");
		}
	} catch (err) {
		console.log("Config error: " + err);
	}
});

Pebble.addEventListener("appmessage", function (e) {
	const payload = e.payload;

	if (payload.action === ACTION_FETCH) {
		const locale = payload.language || "en";
		currentLocale = locale;
		const lib = payload.lib || "lp-e";
		currentLib = lib;
		const rsconf = payload.rsconf || "1";
		currentRsconf = rsconf;
		const parts = payload.date.split("-");
		const year = parseInt(parts[0]);
		const month = parseInt(parts[1]);
		const day = parseInt(parts[2]);
		const totalDays = daysInMonth(year, month);
		const cacheDays = payload.cache_days || 7;

		fetchDailyTextFromWol(payload.date, locale, lib, rsconf, function (result) {
			if (result.error) {
				Pebble.sendAppMessage({
					action: ACTION_FETCH_ERROR,
					date: payload.date,
					error: result.error
				});
			} else {
				sendResult(result.date, locale, lib, rsconf, result.ref, result.text, result.commentary);

				const preFetchEnd = Math.min(day + cacheDays - 1, totalDays);
				if (day + 1 <= preFetchEnd) {
					setTimeout(function () {
						preFetchDays(year, month, day + 1, preFetchEnd, locale, lib, rsconf);
					}, 300);
				}
			}
		});
	}
});
