#!/bin/bash
info=$(curl 'http://d1.weather.com.cn/sk_2d/101010100.html' -H 'Referer: http://www.weather.com.cn/' -H 'user-agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Safari/537.36')
data=$(echo $info | grep -E 'dataSK\s?={([^}]*)}' -o | sed 's/^dataSK[ \t]*=//g')

redis-cli publish weather/forcast "$(echo $data|jq -r .weather)"
redis-cli publish weather/temperature "温度$(echo $data|jq -r .temp)℃"
redis-cli publish weather/humidity "湿度$(echo $data|jq -r .SD|sed 's/%/%%/g')"
redis-cli publish weather/wind "$(echo $data|jq -r .WD)$(echo $data|jq -r .WS)"
redis-cli publish weather/aqi "AQI $(echo $data|jq -r .aqi)"
