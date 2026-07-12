function stripTags(text) {
	return text.replace(/<[^>]+>/g, "").replace(/\s+/g, " ").replace(/\u200b/g, "").trim();
}

function fetchDailyTextFromWol(dateStr, callback) {
	const parts = dateStr.split("-");
	const url = `https://wol.jw.org/en/wol/dt/r1/lp-e/${parseInt(parts[0])}/${parseInt(parts[1])}/${parseInt(parts[2])}`;

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

function getMonthData(year, month) {
	const ym = FULL_DATA[year];
	if (!ym) return null;
	const mm = String(month).padStart(2, "0");
	return ym[mm] || null;
}

Pebble.addEventListener("ready", function () {
	console.log("JW Daily Text phone proxy ready");
});

Pebble.addEventListener("appmessage", function (e) {
	const payload = e.payload;

	if (payload.action === "fetch") {
		fetchDailyTextFromWol(payload.date, function (result) {
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

	if (payload.action === "fetch_month") {
		const year = payload.year;
		const month = payload.month;
		const data = getMonthData(year, month);
		if (data) {
			let idx = 0;
			const keys = Object.keys(data).sort();
			for (const dateStr of keys) {
				const e = data[dateStr];
				Pebble.sendAppMessage({
					action: "month_entry",
					date: dateStr,
					ref: e.ref,
					text: e.text,
					commentary: e.commentary,
					month_total: keys.length,
					month_index: idx + 1
				});
				idx++;
			}
			console.log(`Sent ${keys.length} entries for ${year}-${month}`);
		} else {
			console.log(`No data for ${year}-${month}, falling back to WOL`);
			const daysInMonth = new Date(parseInt(year), parseInt(month), 0).getDate();
			let pending = daysInMonth;
			for (let d = 1; d <= daysInMonth; d++) {
				const ds = year + "-" + String(month).padStart(2, "0") + "-" + String(d).padStart(2, "0");
				fetchDailyTextFromWol(ds, function (result) {
					if (!result.error) {
						Pebble.sendAppMessage({
							action: "month_entry",
							date: result.date,
							ref: result.ref,
							text: result.text,
							commentary: result.commentary,
							month_total: daysInMonth,
							month_index: daysInMonth - pending + 1
						});
					}
					pending--;
				});
			}
		}
	}
});
