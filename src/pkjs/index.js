var CONFIG_URL = "https://testarossa47.github.io/jw-daily-text/config/";
var ACTION_FETCH = 1;
var ACTION_FETCH_RESULT = 2;
var ACTION_FETCH_ERROR = 3;
var ACTION_LANGUAGE_CHANGED = 4;
var ACTION_SYNC_RANGE = 5;
var ACTION_LANG_LIST = 6;
var ACTION_SETTINGS = 7;
var KNOWN_RSCONF = { "en": "1", "de": "10", "es": "4", "ja": "7" };
var LS_PREFIX = "dt.";
var MAX_SWITCH_LANGS = 3;

var currentLocale = "en";
var currentLib = "lp-e";
var currentRsconf = "1";
var currentName = "English";
var phoneCacheDays = 30;
var watchCacheDays = 30;
var textSize = 0;
var switchLangs = [];
var configPending = false;
var configDeliveryRunning = false;
var PHONE_CACHE_MIN = 1;
var PHONE_CACHE_MAX = 30;
var WATCH_CACHE_MIN = 7;
var WATCH_CACHE_MAX = 30;
var importRunning = false;
var importStopped = false;

function clamp(v, lo, hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

function dateToStr(d) {
	return d.getFullYear() + "-" + ("0" + (d.getMonth() + 1)).slice(-2) + "-" + ("0" + d.getDate()).slice(-2);
}

function stripTags(text) {
	return text.replace(/<[^>]+>/g, "").replace(/\s+/g, " ").replace(/\u200b/g, "").trim();
}

function daysInMonth(y, m) {
	return new Date(y, m, 0).getDate();
}

/* Cache keys are per-locale so multiple languages can live on the phone (and,
   via sync, on the watch) at the same time (issue #16). */
function lsKey(locale, dateStr) {
	return LS_PREFIX + locale + ":" + dateStr;
}

function getCached(locale, dateStr) {
	try {
		var raw = localStorage.getItem(lsKey(locale, dateStr));
		return raw ? JSON.parse(raw) : null;
	} catch (e) { return null; }
}

function setCache(locale, dateStr, entry) {
	try { localStorage.setItem(lsKey(locale, dateStr), JSON.stringify(entry)); } catch (e) {}
}

/* Locales whose cached days are kept: the primary language plus the
   watch-switch languages. Everything else is pruned. */
function keptLocales() {
	var keep = [currentLocale];
	for (var i = 0; i < switchLangs.length; i++) {
		if (keep.indexOf(switchLangs[i].locale) < 0) keep.push(switchLangs[i].locale);
	}
	return keep;
}

function clearLocalesExcept(keep) {
	try {
		var keys = Object.keys(localStorage);
		for (var i = 0; i < keys.length; i++) {
			var k = keys[i];
			if (k.indexOf(LS_PREFIX) !== 0) continue;
			var rest = k.substring(LS_PREFIX.length);
			if (rest.indexOf("_") === 0) continue;
			var colon = rest.indexOf(":");
			if (colon < 0) continue;
			if (keep.indexOf(rest.substring(0, colon)) < 0) {
				localStorage.removeItem(k);
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
		var nm = localStorage.getItem(LS_PREFIX + "_name");
		if (nm) currentName = nm;
		var ts = localStorage.getItem(LS_PREFIX + "_textSize");
		if (ts !== null) textSize = parseInt(ts, 10) === 1 ? 1 : 0;
		var sl = localStorage.getItem(LS_PREFIX + "_switchLangs");
		if (sl) {
			var parsed = JSON.parse(sl);
			if (parsed && parsed.length) switchLangs = parsed;
		}
		configPending = localStorage.getItem(LS_PREFIX + "_configPending") === "1";
		var pcd = localStorage.getItem(LS_PREFIX + "_phoneCacheDays");
		var wcd = localStorage.getItem(LS_PREFIX + "_watchCacheDays");
		if (!pcd && !wcd) {
			var legacy = localStorage.getItem(LS_PREFIX + "_cacheDays");
			if (legacy) { pcd = legacy; wcd = legacy; }
		}
		if (pcd) phoneCacheDays = parseInt(pcd, 10) || PHONE_CACHE_MAX;
		if (wcd) watchCacheDays = parseInt(wcd, 10) || WATCH_CACHE_MAX;
	} catch (e) {}
	phoneCacheDays = clamp(phoneCacheDays, PHONE_CACHE_MIN, PHONE_CACHE_MAX);
	watchCacheDays = clamp(watchCacheDays, WATCH_CACHE_MIN, WATCH_CACHE_MAX);
}

function saveState() {
	try {
		localStorage.setItem(LS_PREFIX + "_locale", currentLocale);
		localStorage.setItem(LS_PREFIX + "_lib", currentLib);
		localStorage.setItem(LS_PREFIX + "_rsconf", currentRsconf);
		localStorage.setItem(LS_PREFIX + "_name", currentName);
		localStorage.setItem(LS_PREFIX + "_textSize", String(textSize));
		localStorage.setItem(LS_PREFIX + "_switchLangs", JSON.stringify(switchLangs));
		localStorage.setItem(LS_PREFIX + "_phoneCacheDays", String(phoneCacheDays));
		localStorage.setItem(LS_PREFIX + "_watchCacheDays", String(watchCacheDays));
		localStorage.setItem(LS_PREFIX + "_cacheDays", String(watchCacheDays));
	} catch (e) {}
}

function setConfigPending(pending) {
	configPending = pending;
	try { localStorage.setItem(LS_PREFIX + "_configPending", pending ? "1" : "0"); } catch (e) {}
}

/* Drop cached days outside the retention window (and stale other locales). */
function pruneCache() {
	try {
		var today = new Date();
		today = new Date(today.getFullYear(), today.getMonth(), today.getDate());
		var keepPast = 7;
		var keepFuture = Math.max(phoneCacheDays, watchCacheDays);
		var keep = keptLocales();
		var keys = Object.keys(localStorage);
		for (var i = 0; i < keys.length; i++) {
			var k = keys[i];
			if (k.indexOf(LS_PREFIX) !== 0) continue;
			var rest = k.substring(LS_PREFIX.length);
			if (rest.indexOf("_") === 0) continue;
			var colon = rest.indexOf(":");
			if (colon < 0) continue;
			if (keep.indexOf(rest.substring(0, colon)) < 0) {
				localStorage.removeItem(k);
				continue;
			}
			var parts = rest.substring(colon + 1).split("-");
			if (parts.length !== 3) { localStorage.removeItem(k); continue; }
			var d = new Date(parseInt(parts[0], 10), parseInt(parts[1], 10) - 1, parseInt(parts[2], 10));
			var diff = Math.round((d - today) / 86400000);
			if (diff < -keepPast || diff > keepFuture) localStorage.removeItem(k);
		}
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

function fetchFromWol(locale, lib, dateStr, callback) {
	findRsconf(locale, lib, function(rsconf) {
		if (!rsconf) {
			callback({ error: "Language changed" });
			return;
		}
		if (locale === currentLocale && rsconf !== currentRsconf) {
			currentRsconf = rsconf;
			saveState();
		}
		var parts = dateStr.split("-");
		var url = "https://wol.jw.org/" + locale + "/wol/dt/r" + rsconf + "/" + lib + "/" +
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
			var entry = { ref: ref, text: text, commentary: commentary };
			setCache(locale, dateStr, entry);
			callback({ result: { date: dateStr, ref: ref, text: text, commentary: commentary,
				language: locale, lib: lib, rsconf: rsconf } });
		};
		req.onerror = function () { callback({ error: "Network error" }); };
		req.ontimeout = function () { callback({ error: "Timeout" }); };
		req.send();
	});
}

function getDay(locale, lib, dateStr, callback) {
	var cached = getCached(locale, dateStr);
	if (cached) {
		callback({ result: { date: dateStr, ref: cached.ref, text: cached.text, commentary: cached.commentary,
			language: locale, lib: lib,
			rsconf: localStorage.getItem(LS_PREFIX + "_rsconf:" + locale) || "1" } });
		return;
	}
	fetchFromWol(locale, lib, dateStr, callback);
}

function sendResult(res) {
	Pebble.sendAppMessage({
		action: ACTION_FETCH_RESULT, date: res.date, ref: res.ref, text: res.text, commentary: res.commentary,
		language: res.language, lib: res.lib, rsconf: res.rsconf
	});
}

function sendSettings(success, failure) {
	Pebble.sendAppMessage({ action: ACTION_SETTINGS, text_size: textSize }, success, failure);
}

function truncateUtf8(text, maxBytes) {
	var out = "";
	var used = 0;
	for (var i = 0; i < text.length; i++) {
		var code = text.charCodeAt(i);
		var bytes = code < 0x80 ? 1 : (code < 0x800 ? 2 : 3);
		var chunk = text.charAt(i);
		if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length) {
			var low = text.charCodeAt(i + 1);
			if (low >= 0xdc00 && low <= 0xdfff) {
				bytes = 4;
				chunk += text.charAt(++i);
			}
		}
		if (used + bytes > maxBytes) break;
		out += chunk;
		used += bytes;
	}
	return out;
}

function languageListCsv() {
	var all = [{ locale: currentLocale, lib: currentLib, rsconf: currentRsconf, name: currentName }]
		.concat(switchLangs);
	return all.map(function (l) {
		var name = truncateUtf8((l.name || l.locale).replace(/[|;]/g, " "), 20);
		return l.locale + "|" + l.lib + "|" + (l.rsconf || localStorage.getItem(LS_PREFIX + "_rsconf:" + l.locale) || "1") + "|" + name;
	}).join(";");
}

function sendLangList(success, failure) {
	Pebble.sendAppMessage({ action: ACTION_LANG_LIST, language_list: languageListCsv() }, success, failure);
}

function sendConfiguration(attempt, success) {
	if (attempt === 1) {
		if (configDeliveryRunning) return;
		configDeliveryRunning = true;
	}
	Pebble.sendAppMessage({
		action: ACTION_LANGUAGE_CHANGED,
		language: currentLocale,
		lib: currentLib,
		rsconf: currentRsconf,
		cache_days: watchCacheDays,
		text_size: textSize,
		language_list: languageListCsv()
	}, function () {
		configDeliveryRunning = false;
		setConfigPending(false);
		if (success) success();
	}, function (err) {
		if (attempt < 3) {
			setTimeout(function () { sendConfiguration(attempt + 1, success); }, 500 * attempt);
		} else {
			configDeliveryRunning = false;
			console.log("Configuration delivery failed: " + JSON.stringify(err));
		}
	});
}

function sendDateRange(locale, lib, startDate, endDate) {
	var current = new Date(startDate);
	var end = new Date(endDate);

	function sendNext() {
		if (current > end) return;

		var year = current.getFullYear();
		var month = current.getMonth() + 1;
		var day = current.getDate();
		var dateStr = year + "-" + ("0" + month).slice(-2) + "-" + ("0" + day).slice(-2);

		var cached = getCached(locale, dateStr);
		if (cached) {
			sendResult({ date: dateStr, ref: cached.ref, text: cached.text, commentary: cached.commentary,
				language: locale, lib: lib,
				rsconf: localStorage.getItem(LS_PREFIX + "_rsconf:" + locale) || "1" });
			current.setDate(current.getDate() + 1);
			setTimeout(sendNext, 100);
		} else {
			getDay(locale, lib, dateStr, function (res) {
				if (res.result) {
					sendResult(res.result);
				}
				current.setDate(current.getDate() + 1);
				setTimeout(sendNext, 300);
			});
		}
	}

	sendNext();
}

function preFetchDays(locale, lib, year, month, startDay, endDay) {
	if (startDay > endDay) return;
	var dateStr = year + "-" + ("0" + month).slice(-2) + "-" + ("0" + startDay).toString().slice(-2);
	if (getCached(locale, dateStr)) { preFetchDays(locale, lib, year, month, startDay + 1, endDay); return; }
	getDay(locale, lib, dateStr, function (res) {
		if (res.result) {
			sendResult(res.result);
		}
	});
	setTimeout(function () { preFetchDays(locale, lib, year, month, startDay + 1, endDay); }, 300);
}

function allLangs() {
	return [{ locale: currentLocale, lib: currentLib }].concat(switchLangs);
}

function watchDaysPerLanguage(languageCount) {
	var days = Math.floor((watchCacheDays + 1) / Math.max(1, languageCount)) - 1;
	return Math.max(1, days);
}

/* Cache the full phone horizon for every selected language, but send a shared
   slice of the watch horizon per language so each language keeps today. */
function startYearlyImport(stopIfRunning) {
	if (stopIfRunning) { importStopped = true; return; }
	if (importRunning) return;
	importRunning = true;
	importStopped = false;

	var today = new Date();
	var horizon = Math.max(phoneCacheDays, watchCacheDays);

	var dates = [];
	for (var i = 0; i <= horizon; i++) {
		var d = new Date(today.getFullYear(), today.getMonth(), today.getDate() + i);
		dates.push(dateToStr(d));
	}

	var langs = allLangs();
	var watchHorizon = watchDaysPerLanguage(langs.length);

	function importDay(langIdx, idx) {
		if (importStopped || langIdx >= langs.length) { importRunning = false; return; }
		if (idx >= dates.length) { importDay(langIdx + 1, 0); return; }
		var lang = langs[langIdx];
		var dateStr = dates[idx];
		var cached = getCached(lang.locale, dateStr);
		if (cached) {
			if (idx <= watchHorizon) {
				sendResult({ date: dateStr, ref: cached.ref, text: cached.text, commentary: cached.commentary,
					language: lang.locale, lib: lang.lib,
					rsconf: localStorage.getItem(LS_PREFIX + "_rsconf:" + lang.locale) || "1" });
			}
			setTimeout(function () { importDay(langIdx, idx + 1); }, 100);
			return;
		}
		getDay(lang.locale, lang.lib, dateStr, function (res) {
			if (res.result && idx <= watchHorizon) {
				sendResult(res.result);
			}
			setTimeout(function () { importDay(langIdx, idx + 1); }, 300);
		});
	}
	importDay(0, 0);
}

function restartYearlyImport() {
	importStopped = true;
	function waitForStop() {
		if (importRunning) {
			setTimeout(waitForStop, 250);
			return;
		}
		startYearlyImport(false);
	}
	waitForStop();
}

Pebble.addEventListener("ready", function () {
	loadState();
	pruneCache();
	console.log("JW Daily Text ready: " + currentLocale + " " + currentLib + " r" + currentRsconf +
		" (phone " + phoneCacheDays + "d, watch " + watchCacheDays + "d, textSize " + textSize +
		", switch " + switchLangs.length + ")");
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
	Pebble.openURL(CONFIG_URL + "?v=" + Date.now() + "&locale=" + currentLocale + "&lib=" + currentLib +
		"&rsconf=" + currentRsconf + "&pcd=" + phoneCacheDays + "&wcd=" + watchCacheDays +
		"&ts=" + textSize + "&sl=" + encodeURIComponent(JSON.stringify(switchLangs)));
});

Pebble.addEventListener("webviewclosed", function (e) {
	if (!e.response) return;
	try {
		var config = JSON.parse(decodeURIComponent(e.response));
		if (config && config.locale && config.lib) {
			currentLocale = config.locale;
			currentLib = config.lib;
			currentName = config.name || config.locale;
			if (typeof config.phoneCacheDays === "number") {
				phoneCacheDays = clamp(config.phoneCacheDays, PHONE_CACHE_MIN, PHONE_CACHE_MAX);
			}
			if (typeof config.watchCacheDays === "number") {
				watchCacheDays = clamp(config.watchCacheDays, WATCH_CACHE_MIN, WATCH_CACHE_MAX);
			}
			textSize = (config.textSize === 1 || config.textSize === "1") ? 1 : 0;
			switchLangs = [];
			if (config.switchLangs && config.switchLangs.length) {
				for (var i = 0; i < config.switchLangs.length && switchLangs.length < MAX_SWITCH_LANGS; i++) {
					var l = config.switchLangs[i];
					if (l && l.locale && l.lib && l.locale !== currentLocale) {
						switchLangs.push({ locale: l.locale, lib: l.lib,
							name: l.name || l.locale, rsconf: l.rsconf || "1" });
					}
				}
			}
			clearLocalesExcept(keptLocales());
			findRsconf(currentLocale, currentLib, function(rsconf) {
				if (!rsconf) return;
				currentRsconf = rsconf;
				saveState();
				/* Apply list, primary language, cache window, and text size as one
				   atomic message; retry before starting the replacement import. */
				setConfigPending(true);
				sendConfiguration(1, restartYearlyImport);
				console.log("Language: " + config.name + " (" + currentLocale + " " + currentLib + " r" + currentRsconf +
					", phone " + phoneCacheDays + "d, watch " + watchCacheDays + "d, textSize " + textSize +
					", switch " + switchLangs.length + ")");
			});
		}
	} catch (err) { console.log("Config error: " + err); }
});

Pebble.addEventListener("appmessage", function (e) {
	var payload = e.payload;
	if (configPending) sendConfiguration(1, restartYearlyImport);

	/* Piggyback settings + language list on watch-initiated traffic. They are
	   tiny, and repeating them makes delivery robust: the watch re-requests on
	   every navigation to an uncached day, so a dropped message self-heals. */
	sendSettings();
	sendLangList();

	if (payload.action === ACTION_SYNC_RANGE) {
		var startDate = payload.start_date;
		var endDate = payload.end_date;
		if (startDate && endDate) {
			sendDateRange(payload.language || currentLocale, payload.lib || currentLib, startDate, endDate);
		}
		return;
	}
	if (payload.action !== ACTION_FETCH) return;

	/* Use the requested language without touching the primary phone state:
	   the watch may be browsing a switch language (issue #16). */
	var reqLocale = payload.language || currentLocale;
	var reqLib = payload.lib || currentLib;

	getDay(reqLocale, reqLib, payload.date, function (res) {
		if (res.error) {
			Pebble.sendAppMessage({ action: ACTION_FETCH_ERROR, date: payload.date, error: res.error });
			return;
		}
		sendResult(res.result);
		var parts = payload.date.split("-");
		var year = parseInt(parts[0]);
		var month = parseInt(parts[1]);
		var day = parseInt(parts[2]);
		var totalDays = daysInMonth(year, month);
		var preFetchEnd = Math.min(day + watchDaysPerLanguage(allLangs().length), totalDays);
		if (day + 1 <= preFetchEnd) {
			setTimeout(function () { preFetchDays(reqLocale, reqLib, year, month, day + 1, preFetchEnd); }, 300);
		}
	});
});
