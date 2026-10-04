module.exports = [
  {
    "type": "heading",
    "defaultValue": "Watchface Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Features"
      },
      {
        "type": "select",
        "messageKey": "THEME",
        "defaultValue": "green",
        "label": "Theme",
        "options": [
          {"label": "Green", "value": "green"},
          {"label": "Blue", "value": "blue"},
          {"label": "Red", "value": "red"}
        ]
      },
      {
        "type": "toggle",
        "messageKey": "PERSISTENT_DATA",
        "label": "Display data persistently",
        "defaultValue": false,
      },
      {
        "type": "toggle",
        "messageKey": "TEMP_CELSIUS",
        "label": "Temperature in Celsius",
        "defaultValue": false,
      },
      {
        "type": "slider",
        "messageKey": "STEP_GOAL",
        "label": "Step Count Goal",
        "min": 1000,
        "defaultValue": 10000,
        "max": 30000,
        "step": 1000
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];