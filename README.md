# taixia-pa — ESPHome TaiSEIA 元件 (Panasonic 客製版)

> Fork 自 [tsunglung/taixia](https://github.com/tsunglung/taixia)。原作者
> 已完成 TaiSEIA 101 (CNS 16014) 協定核心、多廠牌支援、HomeAssistant
> 整合等基礎建設，感謝原作者貢獻。
>
> 本 fork **針對 Panasonic 冷氣**做了一些修正與行為調整，部分行為轉成Panasonic 慣例 (詳見下方)。
>
> Python component 名稱**仍為 `taixia`**，原本 YAML 只需要改
> `external_components` 的 `source` 即可切到此版本。

---

## 改了哪些東西

### Bug 修正
- `select` 改值之後 UI 不會立刻更新 (要等下次 polling) — 加上 optimistic
  publish + 3 秒 command lock，跟既有 switch / climate 相同 pattern。
- `select` 收到非自己 service_id 的封包時噴 "Invalid value N" warning —
  改成先過濾自己的 service_id 再查 mapping。
- preset `NONE` 會 reset ECO / SELF_CLEANING / AIR_PURIFIER —
  這些功能改為獨立 switch，preset NONE 不再碰它們以免衝突。
- preset 回讀只反映實際開啟的 BOOST / SLEEP / ACTIVITY，移除
  ECO / AWAY / COMFORT / HOME 的回讀 (避免設定到不支援的 preset)。

### Schema 強化
- `select` 的 `options:` 接受 **list 或 dict** 兩種寫法：
  - **list** (向後相容)：從預設標籤過濾出子集。
  - **dict** (新)：完全 override 標籤對應的數值，可直接寫中文標籤

### 新增 entity
- `select.swing_vertical_level` (H'0F) — Panasonic 只有 level，沒 boolean。
- `text_sensor.boost_mode` — H'1A 狀態反饋，英文 keyword `normal` / `boost` /
  `quiet`，方便 HA 多語系翻譯 (詳見下方〈急速/靜音〉節)。

### Panasonic 行為調整
- **`climate.swing_mode` 改成純反饋**。Panasonic 沒有 H'0E / H'10
  (boolean swing)，只有 H'0F / H'11 (level)。原本 control() 會寫
  H'0E / H'10 / H'11 是無效或會 clobber 使用者的 level 設定，現在改
  成只接受 call 但不寫 UART。`handle_response` 仍會根據
  `H'0F == 0` / `H'11 == 0` 算出 swing_mode 顯示。
  → 葉片位置請用新的 `select.swing_vertical_level` /
  `select.swing_horizontal_level` 控制。
- **`select.motion_detect` (H'19) 跟葉片連動** — 詳見下節。
- **`switch.super_mode` (H'1A=1) 嚴格比對** — handle_response 把 H'1A
  特例化：value==1 才顯示 ON。原本通用 readback 是「任何非 0 = ON」，
  會造成 IR 設靜音 (H'1A=2) 時 switch.super_mode 也亮起的錯誤。

### Debug 輔助
- 每筆 `taixia.climate` polling 回應的 hex dump + 各 H'XX 服務碼解析在
  DEBUG level 印出。預設關閉，需要時 YAML 開啟：
  ```yaml
  logger:
    level: INFO
    logs:
      taixia.climate: DEBUG
  ```

---

## 動向感應 (H'19) 控制邏輯

Panasonic 遙控器按「動向感應」鍵時，AC 內部會自動把上下、左右擺動切到
「自動掃 (level=0)」，這樣感應器才能真正接管葉片方向；關閉時又會把擺動還原。

但**直接寫 H'19 = N，AC 不會有作用**，造成「動向感應好像有開、
但葉片沒動」。本 fork 在 `AirConditionerSelect::control()` 內模擬遙控器行為：

```
動向感應 0 → 非0 (例如選「對人」):
  1. ESP32 記住目前 H'0F / H'11 值 (saved_swing_vert / saved_swing_horiz)
  2. UART 送 H'0F = 0
  3. UART 送 H'11 = 0
  4. publish "level=0 對應 label" 給上下擺動 select  (HA UI 瞬間更新)
  5. publish "level=0 對應 label" 給左右擺動 select  (HA UI 瞬間更新)
  6. UART 送 H'19 = 新值
  7. publish 新 label 給動向感應 select

動向感應 非0 → 0 (選「關閉」):
  1. UART 送 H'19 = 0
  2. UART 送 H'0F = saved_swing_vert (還原)
  3. UART 送 H'11 = saved_swing_horiz (還原)
  4. publish 還原後的 label 給兩個擺動 select
  5. publish 「關閉」label 給動向感應 select

動向感應 非0 → 非0 (例如「對人」→「不對人」):
  - 只送 H'19，不動擺動
```

### 防止誤動作
當 H'19 非 0 (動向感應 active) 時，**上下/左右擺動 select 寫入會被拒絕**：
- `control()` log 出 `Swing change refused: motion_detect is active (H'19=N)`
- 不送 UART
- 把 UI snap 回目前 device 真正的值

要編輯擺動位置請先把動向感應切到「關閉」。

---

## 急速 / 靜音 (H'1A)

- `switch.super_mode` 控制急速 (H'1A on/off)
- `text_sensor.boost_mode` 反饋目前狀態，英文 keyword `normal` / `boost` / `quiet`
  方便 HA 透過 `customize.yaml`、template sensor 或 Lovelace card 翻譯成在地語言

靜音只能由遙控器觸發，本 fork 不嘗試從 TaiSEIA 寫入靜音 — 用 text_sensor 提供
狀態反饋給 HA 自動化使用。

---

## 輪詢間隔 (update_interval)

把 `update_interval` 設在 `sensor:` 區塊裡：

```yaml
sensor:
  - platform: taixia
    type: airconditioner
    update_interval: 10s
    temperature_indoor: ...
```

建議值 10~30 秒。

---

## Panasonic 範例 YAML (部分)

完整請參考 ESP32C3-Panasonic-AC.yaml

```yaml
external_components:
  - source: github://xangin/taixia-pa
    components: [ taixia ]

uart:
  id: uart_taixia
  tx_pin: GPIO7   # → AC RX
  rx_pin: GPIO6   # ← AC TX
  baud_rate: 9600

button:
  - platform: safe_mode
    name: Safe Mode Boot
    entity_category: diagnostic
  - platform: taixia
    type: airconditioner
    get_info:
      name: "Get Info"
    energy_reset:
      name: "Reset Energy"
    filter_clean_notify:
      name: "Clear Filter Notify"

climate:
  - platform: taixia
    id: ac_climate
    name: "Climate"
    supported_modes:
      - COOL
      - HEAT
      - DRY
      - FAN_ONLY
    supported_fan_modes:
      - LOW
      - MEDIUM
      - HIGH
      - AUTO
    supported_swing_modes:
      - VERTICAL
      - HORIZONTAL
      - BOTH
    supported_presets:
      - NONE
      - BOOST
      - ACTIVITY
      - SLEEP

number:
  - platform: taixia
    type: airconditioner
    off_timer:
      name: "Off Timer"
    on_timer:
      name: "On Timer"

sensor:
  - platform: taixia
    type: airconditioner
    update_interval: 10s
    temperature_indoor:
      name: "Temperature Indoor"
    temperature_outdoor:
      name: "Temperature Outdoor"
    operating_current:
      name: "Current"
    energy_consumption:
      state_class: total_increasing
      name: "Energy"
    operating_watt:
      name: "Power"
    error_code:
      name: "Error Code"

select:
  - platform: taixia
    type: airconditioner

    display_mode:
      id: sel_display_mode
      name: "面板燈光"
      options:
        "最亮": 0
        "稍暗": 1
        "關": 2

    motion_detect:
      id: sel_motion_detect
      name: "動向感應"
      options:
        "關閉": 0
        "對人": 1
        "不對人": 2
        "自動": 3

    swing_vertical_level:
      id: sel_swing_vert
      name: "上下擺動位置"
      options:
        "自動擺動": 0
        "1上": 1
        "2中上": 2
        "3中": 3
        "4中下": 4
        "5下": 5

    swing_horizontal_level:
      id: sel_swing_horiz
      name: "左右擺動位置"
      options:
        "自動擺動": 0
        "1中": 1
        "2右偏左": 2
        "3左偏右": 3
        "4最左": 4
        "5左偏中": 5
        "6中偏右": 6
        "7最右": 7

switch:
  - platform: taixia
    type: airconditioner
    power:
      name: "Power Switch"
    beeper:
      name: "Buzzer"
    mildew_proof:
      name: "Mildew Proof"
    self_cleaning:
      name: "Self Cleaning"
    power_saving:
      name: "ECONAVI"
    air_purifier:
      name: "nanoeX"
    super_mode:
      name: "Boost"

text_sensor:
  - platform: taixia
    sa_id:
      name: "SA ID"
      id: sa_id
    brand:
      name: "SA Brand"
    model:
      name: "SA Model"
    version:
      name: "SA Version"
    services:
      name: "SA Services"
    boost_mode:
      name: "Boost Mode"

binary_sensor:
  - platform: taixia
    type: airconditioner
    filter_notify:
      name: "Filter Notify"

taixia:
  sa_id: 1
  response_time: 65000
```

> **注意**：ESPHome 2025.x 要求 `name` 的 ASCII 部分在同 platform 內必須
> 唯一。**純中文** name 會全部轉成 `____` 互相衝突，請在 name 加入至少
> 一個 ASCII 字元，或像上面範例由 `options:` 的 key 提供 ASCII 字符。

---

## License & 致謝

- 原 component：[tsunglung/taixia](https://github.com/tsunglung/taixia)
  by @tsunglung (LICENSE 沿用)
- TaiSEIA 101 協定：CNS 16014 (台灣智慧能源產業協會)
- Panasonic 客製：@xangin
