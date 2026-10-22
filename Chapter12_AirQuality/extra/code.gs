function doGet(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();

  if (e.parameter.mode === "history") {
    return getHistory(sheet, e.parameter.from, e.parameter.to);
  }

  if (e.parameter.mode === "register_ip") {
    sheet.getRange("E1").setValue(e.parameter.ip);
    sheet.getRange("E2").setValue(new Date());
    return ContentService.createTextOutput("IP registered");
  }

  if (e.parameter.mode === "get_ip") {
    var ip = sheet.getRange("E1").getValue();
    return ContentService.createTextOutput(ip || "");
  }

  // --- normal logging path, used by the ESP32 ---
  var pm25 = e.parameter.pm25;
  var pm10 = e.parameter.pm10;
  var alert = e.parameter.alert;
  var email = e.parameter.email;

  var timestamp = new Date();
  sheet.appendRow([timestamp, pm25, pm10]);

  if (alert === "1" && email) {
    MailApp.sendEmail(
      email,
      "Air quality alert",
      "PM2.5 level detected: " + pm25 + " ug/m3 at " + timestamp
    );
  }

  return ContentService.createTextOutput("OK");
}

// Returns rows between fromDate (inclusive, start of day) and toDate
// (inclusive, end of day) as JSON. Dates expected as "yyyy-MM-dd".
function getHistory(sheet, fromStr, toStr) {
  var data = sheet.getDataRange().getValues();
  var fromDate = new Date(fromStr + "T00:00:00");
  var toDate = new Date(toStr + "T23:59:59");

  var result = [];
  for (var i = 1; i < data.length; i++) { // skip header row
    var rowTimestamp = data[i][0];
    if (rowTimestamp instanceof Date && rowTimestamp >= fromDate && rowTimestamp <= toDate) {
      result.push({
        t: Utilities.formatDate(rowTimestamp, Session.getScriptTimeZone(), "yyyy/MM/dd HH:mm"),
        pm25: data[i][1],
        pm10: data[i][2]
      });
    }
  }

  return ContentService.createTextOutput(JSON.stringify(result))
                        .setMimeType(ContentService.MimeType.JSON);
}