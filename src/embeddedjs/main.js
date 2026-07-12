import {} from "piu/MC";
import Button from "pebble/button";
import WakeUp from "pebble/wakeup";
import Message from "pebble/message";
import { texts } from "jw-texts";

const bgSkin = new Skin({ fill: "black" });
const headerSkin = new Skin({ fill: "#1a3a5c" });
const navHeaderSkin = new Skin({ fill: "#5c3a1a" });
const titleStyle = new Style({ font: "bold 16px Gothic", color: "white", horizontal: "left" });
const dateStyle = new Style({ font: "12px Gothic", color: "#88bbdd", horizontal: "left" });
const refStyle = new Style({ font: "bold 14px Gothic", color: "#aaddff", horizontal: "left" });
const scriptureStyle = new Style({ font: "14px Gothic", color: "#d0d0d0", horizontal: "left" });
const commentaryStyle = new Style({ font: "13px Gothic", color: "white", horizontal: "left" });

const MSG_KEYS = new Map([
	["action", 10000],
	["date", 10001],
	["ref", 10002],
	["text", 10003],
	["commentary", 10004],
	["error", 10005],
	["year", 10006],
	["month", 10007],
	["month_total", 10008],
	["month_index", 10009]
]);

function getEntry(dateStr) {
	const year = dateStr ? dateStr.substring(0, 4) : "";
	if (!year) return null;

	const cacheKey = "dt_" + dateStr.replace(/-/g, "_");
	const cached = localStorage.getItem(cacheKey);
	if (cached) {
		try { return JSON.parse(cached); } catch (e) {}
	}

	if (texts[year]) {
		return texts[year][dateStr] || null;
	}
	return null;
}

function todayStr() {
	const now = new Date();
	const y = now.getFullYear();
	const m = String(now.getMonth() + 1).padStart(2, "0");
	const d = String(now.getDate()).padStart(2, "0");
	return `${y}-${m}-${d}`;
}

function formatDisplayDate(dateStr) {
	if (!dateStr) return "";
	const parts = dateStr.split("-");
	const months = ["January","February","March","April","May","June","July","August","September","October","November","December"];
	return `${months[parseInt(parts[1]) - 1]} ${parseInt(parts[2])}, ${parts[0]}`;
}

function getDateKeys(year) {
	const keys = new Set();
	if (texts[year]) {
		for (const k of Object.keys(texts[year])) {
			keys.add(k);
		}
	}
	for (let i = 0; i < localStorage.length; i++) {
		const k = localStorage.key(i);
		if (k && k.startsWith("dt_" + year)) {
			const ds = k.substring(3).replace(/_/g, "-");
			if (ds.length === 10) keys.add(ds);
		}
	}
	return Array.from(keys).sort();
}

function adjustDate(dateStr, direction) {
	const parts = dateStr.split("-");
	const d = new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]) + direction);
	const ny = d.getFullYear();
	const nm = String(d.getMonth() + 1).padStart(2, "0");
	const nd = String(d.getDate()).padStart(2, "0");
	const candidate = `${ny}-${nm}-${nd}`;
	if (getEntry(candidate)) return candidate;
	const keys = getDateKeys(ny);
	if (keys.length > 0) {
		return direction > 0 ? keys[0] : keys[keys.length - 1];
	}
	return dateStr;
}

function scheduleWakeup() {
	try {
		const now = new Date();
		const target = new Date(now);
		target.setDate(now.getDate() + 1);
		target.setHours(7, 0, 0, 0);
		if (target <= now) {
			target.setDate(target.getDate() + 1);
		}
		const id = WakeUp.schedule(target.getTime(), 0, false);
		localStorage.setItem("wakeupId", id.toString());
	} catch (e) {
		console.log(`Wakeup error: ${e}`);
	}
}

function requestMonthFromPhone(dateStr) {
	const parts = dateStr.split("-");
	const msg = new Map();
	msg.set("action", "fetch_month");
	msg.set("year", parts[0]);
	msg.set("month", parts[1]);
	try {
		message.write(msg);
		console.log(`Requested month ${parts[0]}-${parts[1]} from phone`);
	} catch (e) {
		console.log(`Message send error: ${e}`);
	}
}

let monthsRequested = new Set();
let pendingMonths = {};

let navigationMode = false;
let currentDate = todayStr();
let todayCached = currentDate;

const message = new Message({
	keys: MSG_KEYS,
	onReadable() {
		try {
			const data = message.read();
			const action = data.get("action");

			if (action === "month_entry") {
				const date = data.get("date");
				const ref = data.get("ref");
				const text = data.get("text");
				const commentary = data.get("commentary") || "";
				const total = data.get("month_total");
				const index = data.get("month_index");
				if (date && ref) {
					const cacheKey = "dt_" + date.replace(/-/g, "_");
					localStorage.setItem(cacheKey, JSON.stringify({ ref, text, commentary }));
					if (index >= total) {
						const ym = date.substring(0, 7);
						localStorage.setItem("dl_" + ym.replace(/-/g, "_"), "1");
						console.log(`Month ${ym} fully cached`);
					}
				}
				if (date === currentDate || !getEntry(currentDate)) {
					currentDate = date;
					updateDisplay();
				}
				return;
			}

			if (action === "fetch_result") {
				const date = data.get("date");
				const ref = data.get("ref");
				const text = data.get("text");
				const commentary = data.get("commentary") || "";
				if (date && ref) {
					const cacheKey = "dt_" + date.replace(/-/g, "_");
					localStorage.setItem(cacheKey, JSON.stringify({ ref, text, commentary }));
				}
				if (date === currentDate || !getEntry(currentDate)) {
					currentDate = date;
					updateDisplay();
				}
				return;
			}

			if (action === "fetch_error") {
				console.log(`Fetch error for ${data.get("date")}: ${data.get("error")}`);
			}
		} catch (e) {
			console.log(`Message read error: ${e}`);
		}
	}
});

const App = Application.template($ => ({
	skin: bgSkin,
	Behavior: class extends Behavior {
		onCreate(app, data) {
			scheduleWakeup();
		}
		onDisplaying(app) {
			if (watch.wake) {
				console.log(`Launched by wakeup: ${watch.wake.id}`);
			}
		}
	},
	contents: [
		new Container($, {
			anchor: "HEADER",
			top: 0, height: 44, left: 0, right: 0,
			skin: headerSkin,
			contents: [
				new Label($, {
					anchor: "TITLE",
					top: 2, left: 6, right: 6,
					style: titleStyle,
					string: "Daily Text"
				}),
				new Label($, {
					anchor: "DATE",
					top: 22, left: 6, right: 6,
					style: dateStyle,
					string: ""
				})
			]
		}),
		new Content($, {
			anchor: "SCROLLER",
			top: 44, bottom: 0, left: 0, right: 0,
			contents: [
				new Label($, {
					anchor: "REF",
					top: 6, left: 6, right: 6,
					style: refStyle,
					string: ""
				}),
				new Label($, {
					anchor: "SCRIPTURE",
					top: 30, left: 6, right: 6,
					style: scriptureStyle,
					string: ""
				}),
				new Label($, {
					anchor: "COMMENTARY",
					top: 60, left: 6, right: 6,
					style: commentaryStyle,
					string: ""
				})
			]
		})
	]
}));

const app = new App({}, {});

function updateDisplay() {
	const entry = getEntry(currentDate);
	const header = app.content("HEADER");
	const dateLabel = app.content("DATE");
	const refLabel = app.content("REF");
	const scriptureLabel = app.content("SCRIPTURE");
	const commentaryLabel = app.content("COMMENTARY");
	const titleLabel = app.content("TITLE");

	header.skin = navigationMode ? navHeaderSkin : headerSkin;
	titleLabel.string = navigationMode ? "NAV" : "Daily Text";

	if (entry) {
		dateLabel.string = formatDisplayDate(currentDate);
		refLabel.string = entry.ref;
		scriptureLabel.string = entry.text;
		commentaryLabel.string = entry.commentary || "";
	} else {
		dateLabel.string = currentDate;
		refLabel.string = "";
		scriptureLabel.string = "No text available";
		commentaryLabel.string = "";
		const ym = currentDate.substring(0, 7);
		if (!monthsRequested.has(ym)) {
			const dlKey = "dl_" + ym.replace(/-/g, "_");
			if (!localStorage.getItem(dlKey)) {
				monthsRequested.add(ym);
				requestMonthFromPhone(currentDate);
			}
		}
	}
}

updateDisplay();

new Button({
	types: ["up", "down", "select", "back"],
	onPush(down, type) {
		if (!down) return;
		if (type === "select") {
			navigationMode = !navigationMode;
			if (!navigationMode) {
				currentDate = todayCached;
			}
			updateDisplay();
			return;
		}
		if (type === "back") {
			navigationMode = false;
			currentDate = todayCached;
			updateDisplay();
			return;
		}
		const scroller = app.content("SCROLLER");
		if (!scroller) return;
		if (navigationMode) {
			if (type === "up") {
				const newDate = adjustDate(currentDate, -1);
				if (newDate !== currentDate) {
					currentDate = newDate;
					updateDisplay();
				}
			} else if (type === "down") {
				const newDate = adjustDate(currentDate, 1);
				if (newDate !== currentDate) {
					currentDate = newDate;
					updateDisplay();
				}
			}
		} else {
			if (type === "up") {
				scroller.scrollBy(0, -25);
			} else if (type === "down") {
				scroller.scrollBy(0, 25);
			}
		}
	}
});

watch.addEventListener("wakeup", wake => {
	console.log(`Wakeup during runtime: ${wake.id}`);
});
