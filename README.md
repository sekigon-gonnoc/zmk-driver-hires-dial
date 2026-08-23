# ZMK High-resolution Dial Driver

高分解能ダイヤル用ZMKモジュールです。レイヤごとの`sensor-bindings` で、エンコーダ、標準ホイール入力、Radial Controllerを切り替えて動作できます。

## Kconfig

```conf
CONFIG_ZMK_HIRES_DIAL=y
CONFIG_ZMK_HIRES_DIAL_SCROLL=y
CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER=y
```

- `CONFIG_ZMK_HIRES_DIAL` はモジュールとエンコーダbehaviorを有効にします。（必須）
- `CONFIG_ZMK_HIRES_DIAL_SCROLL` は縦・横ホイールbehaviorを有効にします。（オプション）
- `CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER` はUSB/BLEのWindows Radial Controllerを有効にします。（オプション）
  - ペアリング済みのキーボードのファームに新規追加する場合は再ペアリングが必要になる場合があります。

## デバイスツリー

```dts
&i2c0 {
    status = "okay";
    compatible = "nordic,nrf-twim";
    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";
    clock-frequency = <I2C_BITRATE_FAST>;

    hires_dial: hires_dial@75 {
        status = "okay";
        compatible = "sekigon,hires-dial";
        reg = <0x75>;
        res-cpi = <1275>;
        counts-per-revolution = <1275>;
        motion-read-interval-ms = <8>;
        motion-gpios = <&gpio0 20 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
        power-gpios = <&gpio0 8 (GPIO_ACTIVE_HIGH | NRF_GPIO_DRIVE_H0H1)>; // オプション
        sleep1-enable; // オプション
        sleep2-enable; // オプション
    };
};
```


## Behavior

`<behaviors/hires_dial.dtsi>` をincludeして使います。

```dts

#include <behaviors/hires_dial.dtsi>

/ {
    sensors {
        compatible = "zmk,keymap-sensors";
        sensors = <&hires_dial>;
        triggers-per-rotation = <20>;
    };
};

&hires_dial_encoder {
    sensor = <&hires_dial>;
};

&hires_dial_scroll {
    sensor = <&hires_dial>;
};

&hires_dial_hscroll {
    sensor = <&hires_dial>;
};

&hires_dial_radial_controller {
    sensor = <&hires_dial>;
};

&{/keymap/layer_0} {
    sensor-bindings = <&hires_dial_encoder C_VOL_UP C_VOL_DN>;
};

/* 1入力カウントをWHEEL値1へ変換 */
&{/keymap/layer_1} {
    sensor-bindings = <&hires_dial_scroll 1 1>;
};

/* 10入力カウントをWHEEL値1へ変換 */
&{/keymap/layer_2} {
    sensor-bindings = <&hires_dial_scroll 10 1>;
};

&{/keymap/layer_3} {
    sensor-bindings = <&hires_dial_radial_controller 4 1>;
};
```

Radial Controllerの押下は通常のキーポジションへ
`&hires_dial_radial_controller_button` を割り当てます。

Scroll, HScroll, Radial Controllerの2パラメータは `INPUT_MOTION OUTPUT_MOTION` です。
`<&hires_dial_radial_controller 4 1>` は物理回転を1/4に縮小し、`<2 1>` は1/2、
`<1 1>` は等倍で処理します。
