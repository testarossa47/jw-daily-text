var CONFIG_URL = "https://testarossa47.github.io/jw-daily-text/config/";
var ACTION_FETCH = 1;
var ACTION_FETCH_RESULT = 2;
var ACTION_FETCH_ERROR = 3;
var ACTION_LANGUAGE_CHANGED = 4;
var KNOWN_RSCONF = { "de": "10", "es": "4", "ja": "7" };
var LS_PREFIX = "dt.";

var currentLocale = "en";
var currentLib = "lp-e";
var currentRsconf = "1";
var cacheDays = 7;
var importRunning = false;
var importStopped = false;

function stripTags(text) {
	return text.replace(/<[^>]+>/g, "").replace(/\s+/g, " ").replace(/\u200b/g, "").trim();
}

function daysInMonth(y, m) {
	return new Date(y, m, 0).getDate();
}

function lsKey(dateStr) {
	return LS_PREFIX + currentLocale + ":" + dateStr;
}

function getCached(dateStr) {
	try {
		var raw = localStorage.getItem(lsKey(dateStr));
		return raw ? JSON.parse(raw) : null;
	} catch (e) { return null; }
}

function setCache(dateStr, entry) {
	try { localStorage.setItem(lsKey(dateStr), JSON.stringify(entry)); } catch (e) {}
}

function clearLocaleCache() {
	try {
		var keep = {};
		var keys = Object.keys(localStorage);
		for (var i = 0; i < keys.length; i++) {
			if (keys[i].indexOf(LS_PREFIX) === 0) {
				if (keys[i].indexOf("_") === LS_PREFIX.length) continue;
				localStorage.removeItem(keys[i]);
			}
		}
	} catch (e) {}
}

function loadState() {
	try {
		var loc = localStorage.getItem(LS_PREFIX + "_locale");
		if (loc) currentLocale = loc;
		var lb = localStorage.getItem(LS_PREFIX + "_lib");
		if (lb) currentLib = lb;
		var rc = localStorage.getItem(LS_PREFIX + "_rsconf");
		if (rc) currentRsconf = rc;
		var cd = localStorage.getItem(LS_PREFIX + "_cacheDays");
		if (cd) cacheDays = parseInt(cd, 10) || 7;
	} catch (e) {}
	if (cacheDays < 1) cacheDays = 1;
	if (cacheDays > 14) cacheDays = 14;
}

function saveState() {
	try {
		localStorage.setItem(LS_PREFIX + "_locale", currentLocale);
		localStorage.setItem(LS_PREFIX + "_lib", currentLib);
		localStorage.setItem(LS_PREFIX + "_rsconf", currentRsconf);
		localStorage.setItem(LS_PREFIX + "_cacheDays", String(cacheDays));
	} catch (e) {}
}

function findRsconf(locale, lib, callback) {
	var cached = localStorage.getItem(LS_PREFIX + "_rsconf:" + locale);
	if (cached) {
		callback(cached);
		return;
	}
	if (KNOWN_RSCONF[locale]) {
		localStorage.setItem(LS_PREFIX + "_rsconf:" + locale, KNOWN_RSCONF[locale]);
		callback(KNOWN_RSCONF[locale]);
		return;
	}
	var today = new Date();
	var year = today.getFullYear();
	var month = today.getMonth() + 1;
	var day = today.getDate();

	function tryRsconf(rsconf) {
		if (rsconf > 20) {
			callback("1");
			return;
		}
		if (locale !== currentLocale) {
			callback(null);
			return;
		}
		var url = "https://wol.jw.org/" + locale + "/wol/dt/r" + rsconf + "/" + lib + "/" + year + "/" + month + "/" + day;
		var req = new XMLHttpRequest();
		req.open("GET", url, true);
		req.timeout = 5000;
		req.onload = function() {
			if (req.status === 200) {
				localStorage.setItem(LS_PREFIX + "_rsconf:" + locale, String(rsconf));
				callback(String(rsconf));
			} else {
				tryRsconf(rsconf + 1);
			}
		};
		req.onerror = function() { tryRsconf(rsconf + 1); };
		req.ontimeout = function() { tryRsconf(rsconf + 1); };
		req.send();
	}
	tryRsconf(1);
}

function fetchFromWol(dateStr, callback) {
	findRsconf(currentLocale, currentLib, function(rsconf) {
		if (rsconf === null) {
			callback({ error: "Language changed" });
			return;
		}
		if (rsconf !== currentRsconf) {
			currentRsconf = rsconf;
			saveState();
		}
		var parts = dateStr.split("-");
		var url = "https://wol.jw.org/" + currentLocale + "/wol/dt/r" + currentRsconf + "/" + currentLib + "/" +
			parseInt(parts[0]) + "/" + parseInt(parts[1]) + "/" + parseInt(parts[2]);

		var req = new XMLHttpRequest();
		req.open("GET", url, true);
		req.timeout = 15000;
		req.onload = function () {
			if (req.status !== 200) {
				callback({ error: "HTTP " + req.status });
				return;
			}
			var html = req.responseText;
			var themeMatch = html.match(/<p[^>]*class="themeScrp"[^>]*>([\s\S]*?)<\/p>/);
			if (!themeMatch) { callback({ error: "No themeScrp found" }); return; }
			var themeHtml = themeMatch[1];
			var refMatch = themeHtml.match(/<a[^>]*>([\s\S]*?)<\/a>/);
			var ref = refMatch ? stripTags(refMatch[1]) : "";
			var emMatches = themeHtml.match(/<em>([\s\S]*?)<\/em>/g) || [];
			var textParts = [];
			for (var i = 0; i < emMatches.length; i++) {
				var inner = emMatches[i].replace(/<\/?em>/g, "").replace(/<a[^>]*>[\s\S]*?<\/a>/, "").trim();
				var cleaned = stripTags(inner);
				if (cleaned && cleaned.indexOf(ref.replace(/\s+/g, " ").trim()) === -1) {
					textParts.push(cleaned);
				}
			}
			var text = textParts.join(" ").replace(/,+$/, "").trim();
			var bodyMatch = html.match(/<div class="bodyTxt">([\s\S]*?)<\/div>/);
			var commentary = "";
			if (bodyMatch) {
				var pMatch = bodyMatch[1].match(/<p[^>]*>([\s\S]*?)<\/p>/);
				if (pMatch) {
					commentary = stripTags(pMatch[1]);
					var dash = commentary.lastIndexOf("\u2014");
					if (dash > 0) commentary = commentary.substring(0, dash).trim();
					commentary = commentary.replace(/\s*\.\s*$/, "");
				}
			}
			var result = { date: dateStr, ref: ref, text: text, commentary: commentary };
			var entry = { ref: ref, text: text, commentary: commentary };
			setCache(dateStr, entry);
			callback({ result: result });
		};
		req.onerror = function () { callback({ error: "Network error" }); };
		req.ontimeout = function () { callback({ error: "Timeout" }); };
		req.send();
	});
}

function getDay(dateStr, callback) {
	var cached = getCached(dateStr);
	if (cached) {
		callback({ result: { date: dateStr, ref: cached.ref, text: cached.text, commentary: cached.commentary } });
		return;
	}
	fetchFromWol(dateStr, callback);
}

function sendResult(dateStr, ref, text, commentary) {
	Pebble.sendAppMessage({
		action: ACTION_FETCH_RESULT, date: dateStr, ref: ref, text: text, commentary: commentary,
		language: currentLocale, lib: currentLib, rsconf: currentRsconf
	});
}

function preFetchDays(year, month, startDay, endDay) {
	if (startDay > endDay) return;
	var dateStr = year + "-" + ("0" + month).slice(-2) + "-" + ("0" + startDay).toString().slice(-2);
	if (getCached(dateStr)) { preFetchDays(year, month, startDay + 1, endDay); return; }
	getDay(dateStr, function (res) {
		if (res.result) {
			sendResult(res.result.date, res.result.ref, res.result.text, res.result.commentary);
		}
	});
	setTimeout(function () { preFetchDays(year, month, startDay + 1, endDay); }, 300);
}

function startYearlyImport(stopIfRunning) {
	if (stopIfRunning) { importStopped = true; return; }
	if (importRunning) return;
	importRunning = true;
	importStopped = false;

	var today = new Date();
	var year = today.getFullYear();
	var month = today.getMonth() + 1;
	var day = today.getDate();

	var dates = [];
	for (var m = month; m <= 12; m++) {
		var total = daysInMonth(year, m);
		var start = (m === month) ? day : 1;
		for (var d = start; d <= total; d++) {
			dates.push(year + "-" + ("0" + m).slice(-2) + "-" + ("0" + d).toString().slice(-2));
		}
	}

	function importNext(idx) {
		if (importStopped || idx >= dates.length) { importRunning = false; return; }
		var dateStr = dates[idx];
		if (getCached(dateStr)) { importNext(idx + 1); return; }
		getDay(dateStr, function (res) {
			if (res.result) {
				sendResult(res.result.date, res.result.ref, res.result.text, res.result.commentary);
			}
			setTimeout(function () { importNext(idx + 1); }, 300);
		});
	}
	importNext(0);
}

Pebble.addEventListener("ready", function () {
	loadState();
	console.log("JW Daily Text ready: " + currentLocale + " " + currentLib + " r" + currentRsconf);
	findRsconf(currentLocale, currentLib, function(rsconf) {
		if (rsconf && rsconf !== currentRsconf) {
			currentRsconf = rsconf;
			saveState();
			console.log("Updated rsconf to: " + rsconf);
		}
		startYearlyImport(false);
	});
});

Pebble.addEventListener("showConfiguration", function () {
	Pebble.openURL(CONFIG_URL + "?v=" + Date.now() + "&locale=" + currentLocale + "&lib=" + currentLib + "&rsconf=" + currentRsconf);
});

Pebble.addEventListener("webviewclosed", function (e) {
	if (!e.response) return;
	try {
		var config = JSON.parse(decodeURIComponent(e.response));
		if (config && config.locale && config.lib) {
			var oldLocale = currentLocale;
			currentLocale = config.locale;
			currentLib = config.lib;
			cacheDays = config.cacheDays || 7;
			if (cacheDays < 1) cacheDays = 1;
			if (cacheDays > 14) cacheDays = 14;
			if (currentLocale !== oldLocale) {
				clearLocaleCache();
			}
			findRsconf(currentLocale, currentLib, function(rsconf) {
				if (rsconf === null) return;
				currentRsconf = rsconf;
				saveState();
				Pebble.sendAppMessage({
					action: ACTION_LANGUAGE_CHANGED,
					language: currentLocale, lib: currentLib, rsconf: currentRsconf, cache_days: cacheDays
				});
				startYearlyImport(true);
				setTimeout(function () { startYearlyImport(false); }, 500);
				console.log("Language: " + config.name + " (" + currentLocale + " " + currentLib + " r" + currentRsconf + ")");
			});
		}
	} catch (err) { console.log("Config error: " + err); }
});

Pebble.addEventListener("appmessage", function (e) {
	var payload = e.payload;
	if (payload.action !== ACTION_FETCH) return;

	var locale = payload.language || currentLocale;
	currentLocale = locale;
	currentLib = payload.lib || currentLib;
	cacheDays = payload.cache_days || cacheDays;
	saveState();

	findRsconf(currentLocale, currentLib, function(rsconf) {
		if (rsconf === null) return;
		currentRsconf = rsconf;
		saveState();

		var parts = payload.date.split("-");
		var year = parseInt(parts[0]);
		var month = parseInt(parts[1]);
		var day = parseInt(parts[2]);
		var totalDays = daysInMonth(year, month);

		getDay(payload.date, function (res) {
			if (res.error) {
				Pebble.sendAppMessage({ action: ACTION_FETCH_ERROR, date: payload.date, error: res.error });
				return;
			}
			sendResult(res.result.date, res.result.ref, res.result.text, res.result.commentary);
			var preFetchEnd = Math.min(day + cacheDays - 1, totalDays);
			if (day + 1 <= preFetchEnd) {
				setTimeout(function () { preFetchDays(year, month, day + 1, preFetchEnd); }, 300);
			}
		});
	});
});
