#ifndef HTML_CONTENT_H

#define HTML_CONTENT_H

// 使用PROGMEM存储大字符串，节省RAM（针对ESP8266/ESP32）

const char htmlForm[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>WiFi 配置</title>
<meta charset="utf-8">
<meta http-equiv="X-UA-Compatible" content="IE=edge">
<meta name="viewport" content="width=device-width, user-scalable=no, initial-scale=1.0, maximum-scale=1.0, minimum-scale=1.0,viewport-fit=cover">
<meta name="apple-touch-fullscreen" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="default">
    <style>
    body{
      margin: 0px;
      padding:0px;
        background:#382F21 url(data:image/png;base64,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);
        background-size:auto auto;
    }
  input, select, button {
    width: 100%; /* 100%宽度 */
    padding: 18px 10px; /* 内边距 */
    margin: 6px 0; /* 外边距 */
    border: none; /* 无边框 */
    border-radius: 4px; /* 圆角 */
    background-color: #CFBE89; /* 背景色 */
    color: #fff; /* 字体颜色 */
    font-size: 16px; /* 字体大小 */
    transition: background-color 0.3s; /* 背景色过渡效果 */
    box-sizing: border-box; /* 包括内边距和边框 */
  }

  input:focus, select:focus, button:focus {
    outline: none; /* 去掉焦点轮廓 */
  }
  input::placeholder {
    color: #F1F1F1; /* 字体颜色 */
    font-style: italic; /* 字体样式 */
    opacity: 0.7; /* 使颜色不透明 */
  }
  button {
    background-color: #B79C4D !important;  /* 按钮背景色 */
    color: #000; /* 按钮字体颜色 */
    cursor: pointer; /* 鼠标指针 */
  }

  </style>

  </head>
  <body>
    <div style="font-size: 24px;color:#CFBE89;background:#000000;text-align: center;padding:20px;">WiFi 配置</div>
    <div style="padding:20px">
        <form action="/submit" method="POST">
          <select name="select" id="wifiSelect" onchange="document.getElementById('ssidInput').value=this.value">
          <option value="" selected>请选择WIFI</option>
)
)";