# CPyMO 开发与移植指南

这份文档写给两种人：

* 想从零写一个 CPyMO 游戏的人（或 AI）。
* 手上有一份老 PyMO 游戏，想把它搬到 CPyMO 上跑的人。

两者的第一步是一样的：先搞清楚一个游戏目录里有什么。所以从第 1 节顺序读下去即可；只想移植的可以直接跳第 9 节，但第 2 节（`gameconfig.txt`）仍然必须读。

单条命令的参数、`#命令` 与 `@命令` 的对照，见 [CPyMO 指令参考.md](CPyMO%20指令参考.md)。本文只讲「怎么把它们组装成一个游戏」。

本文出现的每条结论都标了来源：

* **引擎**：能在 CPyMO 源码里指到具体行为，可信。
* **手册**：来自 `PYMO_Manual_v1.2.0.md`（PyMO 时代的中文手册）。PyMO 是 Python/S60 上的老引擎，CPyMO 重新实现了它但并非逐字节兼容，手册里过时的地方本文会标注。
* **推测**：没有直接证据，只是合理猜测。看到这三个字请自己验证一遍。

---

## 0. 先选一条路

| 你的情况 | 走哪条 | 从哪读 |
| --- | --- | --- |
| 全新游戏，愿意写 YukimiScript | 写 `.ykm`，用 `ykmc` 编译成 `script/*.txt` | 第 4 节 |
| 全新游戏，想直接手写脚本 | 直接写 PyMO 风格的 `#命令` 文本 | 第 4 节 |
| 已有 PyMO 游戏（`.py` 脚本或已解密的 `#命令` 文本） | `cpymo-tool convert` + 直接跑或先转 ykm | 第 9 节 |
| 已有别的引擎的游戏（KiriKiri、Ren'Py、RPG Maker、自研…） | 素材提到 CPyMO 的目录、脚本重写成命令流 | 从 9.5 读起 |

几条路最后产出的都是同一个东西：一个含 `gameconfig.txt` 和 `script/` 的目录。CPyMO 只认这个目录，不关心你用哪种语法写的脚本。

---

## 1. 一个游戏目录长什么样

**引擎**读取的是 `<游戏目录>/gameconfig.txt` 与 `<游戏目录>/script/<名字>.txt`。目录里其余子目录按约定命名：

```
MyGame/
├── gameconfig.txt          必须
├── icon.png                可选，图标，建议 57×57
├── bg/                     背景图
├── bgm/                    背景音乐
├── chara/                  立绘及其遮罩
├── save/                   存档（引擎运行时写入，不需要预置）
├── script/                 脚本
├── se/                     音效
├── system/                 界面图片与字体
├── video/                  影片，只认 .mp4（DISABLE_MOVIE 构建不可用）
└── voice/                  语音
```

只有 `gameconfig.txt` 和 `script/` 是硬性要求，其余目录按你的游戏是否需要来建。缺目录不会报错，只是对应命令没有资源可用。

`save/` 里实际会出现的三类文件（都在 `<游戏目录>/save/` 下，路径由后端的 `cpymo_backend_save.c` 拼 `<gamedir>/save/<文件名>`，目录本身由后端启动时 `mkdir`）：

* `save-00.csav`、`save-01.csav` …… `save-30.csav` —— 对话界面「存档 / 读档」的 **31 个槽位**（`cpymo_save_ui.c` 里 `CPYMO_MAX_SAVES` 是 31，编号从 0 起，文件名格式 `save-%02d.csav`）。
* `global.csav` —— `S` 开头的全局变量（见 5.3）。
* `config.csav` —— 玩家在设置界面改过的字号、音量、打字速度等，它会盖掉一部分 `gameconfig.txt`（见 2.4）。

这三个都只由引擎写，发布时不用预置；删掉等于重置。注意 `save-%02d` 是两位编号，但槽位到 30，所以是 `save-00` 到 `save-30`，不是三位数。

### 1.1 pak 打包（可选）

**引擎**启动时按固定名字打开四个包（`cpymo_assetloader.c:30/40/50/60`）：

* `bg/bg.pak`、`chara/chara.pak`、`se/se.pak`、`voice/voice.pak`

打不开就当没有。**但某个包一旦存在，这个目录就只从包里读，不再看散文件，也不会回落**（`load_image_pixels` 是 `if (using_pkg) 读包 else 读文件` 的二选一）。所以「大文件打包、零碎的散着放」是不行的：要么整个目录一起打包，要么整个目录都不打。

没有 `system.pak` 和 `bgm.pak`。所以 `system/`、`bgm/`、`script/`、`video/` 里的文件必须以散文件形式存在。

打包用 `cpymo-tool pack`（见 8.1）。打不打 pak 对游戏逻辑没有影响，只影响文件数量和读取开销，小游戏不必打。

`convert` 和 `strip` 走的是同一套资源遍历（`cpymo_tool_asset_filter.c`）：**输入目录里有包就写回包，没有就写散文件**，加 `--pack` 强制输出成包（`cpymo-tool/main.c:76-78`）。所以转换一个带 `bg.pak` 的游戏不会在磁盘上冒出一堆散图。

### 1.2 文件命名

资源名在脚本里不带扩展名，扩展名由 `gameconfig.txt` 的 `bgformat` / `charaformat` 等键决定。所以：

```
bg/BG001.jpg      ←→  脚本里写 BG001
chara/A.png       ←→  脚本里写 A
```

改名时只改前半段，不要顺手改扩展名，否则 `gameconfig.txt` 也得跟着改。

---

## 2. gameconfig.txt

一行一个键值，**逗号分隔**，`key,value`。行首尾空白会被削减。`#` 不是注释符，引擎不认注释（**引擎**：`cpymo_gameconfig.c` 的 `cpymo_dispatch_gameconfig` 只按逗号切，没有注释分支）。

### 2.1 最小可用集合

一个新游戏真正需要写的只有这几行：

```
gametitle,我的第一个游戏
engineversion,1.2
startscript,start
scripttype,pym
platform,pygame
imagesize,1280,720
bgformat,jpg
charaformat,png
charamaskformat,png
bgmformat,mp3
seformat,mp3
voiceformat,mp3
```

`startscript` 是启动时进入的脚本名，对应 `script/<名字>.txt`。**这一项必须写**：引擎不会替你补默认值，空着会去找 `script/.txt` 然后启动失败（**引擎**：`cpymo_script.c` 用 `script/%s.txt` 拼路径，而 `cpymo_gameconfig.c` 的默认值段没有填 `startscript`）。

`imagesize` 是你的**逻辑分辨率**，所有坐标、百分比、立绘缩放都以它为基准。它不必等于窗口大小：窗口会把逻辑画面等比缩放到窗口里。做横屏 AVG 常用 `1280,720` 或 `800,600`；老 PyMO 素材是 `320,240`（S60v3）或 `540,360`（S60v5）。

### 2.2 完整键表（以引擎为准）

| 键 | 引擎默认值 | 含义 |
| --- | --- | --- |
| `gametitle` | 空 | 标题，最长 255 字节。可以含 `\n` 换行 |
| `startscript` | 空（必须自己写） | 启动脚本名，最长 63 字节 |
| `scripttype` | 空 | 写 `pym`。字段只有 4 字节，解析时只留前 3 个字符并强制补 `\0`（`cpymo_str_copy`），所以 `pym` / `pymo` / `pymx` 存下来一模一样；**留空才**会在启动时弹「非 PyMO 脚本」警告（`cpymo_engine.c:271-274` 逐字节比对 `p y m \0`） |
| `platform` | `pygame` | 平台标记，只影响两件事：① 以 `s60v` 开头时启用塞班那套 `_mask` 遮罩（见 `charamaskformat`）；② 等于 `cpymohd1` / `cpymohd2` / `cpymohd3` 时把 feature level 设成 1 / 2 / 3（`cpymo_engine.c:116-124`），也就是**用这一行给游戏开 Level 1 的 Lua 能力**。其它值一律 Level 0。Level 2 / 3 的能力规划见仓库根的 `CPyMO HD.md`。**注意**：`cpymo-tool` 的 `strip` / `convert` 只接受 `pygame` / `s60v3` / `s60v5`，遇到 `cpymohd1/2/3` 会打印 `[Error] Platform ... is not supported.` 直接退出——要过工具链就先把它临时改成 `pygame`（删掉这一行也等于 `pygame`） |
| `engineversion` | 空 | 形如 `1.2`，按 `.` 拆成主次版本 |
| `imagesize` | `0,0` | 逻辑分辨率，`宽,高` |
| `bgformat` | 空 | 背景扩展名，`jpg` 或 `.jpg` 都行 |
| `charaformat` | 空 | 立绘扩展名，同上 |
| `charamaskformat` | 空 | 立绘遮罩扩展名。**只有 `platform` 以 `s60v` 开头（不分大小写，实际就是 `s60v3` / `s60v5`）时才会去加载 `<名字>_mask.<这个扩展名>`**，其它平台一律忽略（**引擎**：`cpymo_assetloader.c` 的 `cpymo_assetloader_load_image_with_mask` 先判 `cpymo_gameconfig_is_symbian`）。遮罩缺失不致命，会退回不带遮罩的普通图 |
| `bgmformat` | `mp3` | 背景音乐扩展名 |
| `seformat` | `mp3` | 音效扩展名 |
| `voiceformat` | `mp3` | 语音扩展名 |
| `cgprefix` | `EV_` | CG 文件名前缀，决定哪些图进鉴赏 |
| `fontsize` | 4 | 字号，被夹到 4–1024；实际几乎总被 `save/config.csav` 覆盖 |
| `textcolor` | 白 | 正文文字颜色，**必须写 `#RRGGBB`**，写 `white` 会变品红 |
| `msgtb` | `6,0` | 文本框上下留白，**一个键两个数**：`上,下`（字段名是 `msgtb_t`/`msgtb_b`，但配置文件里只有 `msgtb`） |
| `msglr` | `10,7` | 文本框左右留白，`左,右`（字段 `msglr_l`/`msglr_r`） |
| `nameboxorig` | `0,0` | 名字框偏移，`x,y`，各夹到 ±1024（字段 `nameboxorg_x`/`nameboxorg_y`） |
| `textspeed` | `3` | 打字速度，3 位（0–7） |
| `bgmvolume` | `0` | 初始 BGM 音量，4 位（0–15） |
| `vovolume` | `0` | 初始语音音量，4 位（0–15） |
| `hint` | `1` | 是否显示操作提示 |
| `grayselected` | `1` | 选中项是否灰化 |
| `playvideo` | `1` | 是否允许播影片 |
| `namealign` | `0` | 名字对齐：`l`/`L`→1 左，`m`/`M`→0 中，`r`/`R`→2 右 |

`bgformat` 这类扩展名键允许带点也允许不带（**引擎**：`SETUP_EXT` 宏两种都吃）。

`msgtb`、`msglr`、`nameboxorig` 这三个键要在**同一行**用逗号给两个数，比如 `msgtb=6,0`。写成 `msgtb_t=6` 不会被识别：引擎按整键名精确比对，认不出的键**静默丢掉**（`cpymo_dispatch_gameconfig` 是一条 if-else 链，没有兜底分支，也不打警告），值仍是默认值。所以「改了配置没反应」第一个要查的就是键名拼写。

`platform` 值会被削减空白，`imagesize` 用逗号分两段。

### 2.3 手册里有、CPyMO 里没有的键

老手册第 4 章列的下面这几个键 **CPyMO 不读**，写了也无害，但没有任何效果：

* `font`：CPyMO 不用数字选字体，字体是后端加载的字体文件（见 7.3）。
* `fontaa`：抗锯齿是后端行为，不由游戏配置控制。
* `prefetching`：CPyMO 的 `bgm` 预取由 `music_list` 机制另行处理。

反过来，CPyMO 有而手册没有的键：`cgprefix`、`msgtb`、`msglr`、`nameboxorig`、`textcolor`、`vovolume`。

### 2.4 `save/config.csav` 会盖掉一部分配置

`save/config.csav` 里存了六个数（每个 `uint16`）：BGM / SE / VO 三路音量、`fontsize`、`textspeed`、还有「跳过已读文本」开关（`cpymo_save_global.c:175-182`）。**它一旦存在，这几个值就以它为准，改 `gameconfig.txt` 不再生效。**

关键在于它不是「玩家进过设置界面才有」的：

* 关闭设置界面时会写一次（`cpymo_config_ui.c:341`）；
* **引擎正常退出时也会写一次**（`cpymo_engine.c:307`，`while (engine->ui) cpymo_ui_exit` 之后）。

所以只要跑过一次并正常退出，这个文件就在了。调试时如果发现 `fontsize`、`textspeed` 改了没反应，先删掉 `save/config.csav` 再跑。

这条在测试里经常踩到：**跑测试游戏前先确认 `save/config.csav` 不存在**（跑完记得再删）。

文件坏了不会崩：读失败只打印 `[Error] Config data broken!`（`cpymo_engine.c:254-256`），然后按 `gameconfig.txt` 的默认值继续。

---

## 3. 坐标系、颜色、时间

### 3.1 百分比坐标

绝大多数位置参数是**百分比**（0–100），相对于 `imagesize`。

* `50,50` 是画面正中。
* `3,67,50,100` 是左起 3%、上起 67% 到左起 50%、上起 100% 的矩形（文本框常用）。
* 少数命令接受绝对像素（例如 `text` 的四个角）。

具体每个参数用哪种，看 [CPyMO 指令参考.md](CPyMO%20指令参考.md) 里对应命令那一行。

### 3.2 颜色

颜色一律写 `#RRGGBB`（六位十六进制），**不认颜色名**。写 `white`、`red` 得到的是品红，不是白色。

**引擎**：`cpymo_str_as_color` 先看首字符是不是 `#`，不是就直接返回 `cpymo_color_error`（品红 241,75,233）；然后只检查 `#` 后面是否全是十六进制字符，从**末位往前**依次填 `b`、`g`、`r`。三位简写（`#FFF`）不报错，但按这个方向填出来是 `(15,255,255)` 而不是白色，**别用三位简写**。

### 3.3 时间

所有 `time` 参数单位是毫秒。`300` 是 0.3 秒，写 `0` 是瞬间完成。

### 3.4 立绘坐标模式（cm0–cm6）

立绘的定位命令（`chara_pos`、`chara_y`、`chara_scroll`、各 `*_multi` 收集行等）最后一个参数是坐标模式，取值 0–6。**在 PyMO 文本脚本里必须写纯数字**（写 `5`），不要写 `cm5`：引擎用 `atoi` 取这个参数（**引擎**：`cpymo_str.c` 的 `cpymo_str_atoi`），`atoi("cm5")` 得到 `0`，立绘会莫名其妙跑到左上角。`cm5` 这种写法属于 YukimiScript 侧（`libpymo.ykm` 把它声明成类型，`macro coord_mode=cm5`），由 `ykmc` 编译成数字。**引擎** `cpymo_charas.c` 的语义是：

| 模式 | 水平 | 垂直 |
| --- | --- | --- |
| cm0 | 立绘左沿到屏幕左沿 | 立绘上沿到屏幕上沿 |
| cm1 | 立绘中点 | 上沿 |
| cm2 | 立绘右沿到屏幕右沿 | 上沿 |
| cm3 | 立绘中点 | 立绘中点 |
| cm4 | 左沿 | 立绘下沿到屏幕下沿 |
| cm5 | 中点 | 下沿 |
| cm6 | 右沿 | 下沿 |

省略这个参数时按 5 处理：水平居中、脚踩屏幕底边。单条 `#chara` 命令**不接受**这个参数，永远按 5 摆。

`cm7` 以上引擎直接返回 `CPYMO_ERR_INVALID_ARG`（**引擎**：`cpymo_chara_convert_to_mode0_pos` 里判 `coord_mode > 6`）。手册里如果有更高编号，那是 PyMO 的别的约定，不要照抄。

---

## 4. 十五分钟写出第一个游戏

### 4.1 准备目录

```bash
mkdir -p MyGame/{script,bg,chara,system,bgm,se,voice,save}
```

放一张背景到 `bg/BG001.jpg`，一张立绘到 `chara/A.png`。没有素材也能先跑：把下面脚本里 `bg` / `chara` 那两行注释掉（实际上引擎不认注释，删掉即可）。

### 4.2 写 gameconfig.txt

```
gametitle,我的第一个游戏
engineversion,1.2
startscript,start
scripttype,pym
platform,pygame
imagesize,1280,720
bgformat,jpg
charaformat,png
charamaskformat,png
bgmformat,mp3
seformat,mp3
voiceformat,mp3
fontsize,24
```

### 4.3 写 script/start.txt

```
#label start
#bg BG001,BG_NOFADE
#chara 1,A,50,0,300
#bgm BGM001
#say 旁白,这是一个 CPyMO 游戏。
#say A,你好，我是 A。
#se SE001
#say A,刚才的按键有声音吗？
#bgm_stop
#change start
```

存成 **UTF-8**（无 BOM 最保险，有 BOM 时脚本名/第一行可能带上脏字符），换行 CRLF / LF 都行（**引擎**：载入时会做 CR 替换）。

`#say` 取两个参数：`名字,正文`。**只写一个参数（没有逗号）时整个串当正文**，名字框为空（**引擎**：`cpymo_interpreter.c` 的 `D("say")` 分支，`POP_ARG(name_or_text)` 之后若第二参为空则对调）。

`#chara` 的参数顺序是 **`编号,资源名,x 百分比,层`**，不是「名字、位置、坐标模式」（**引擎**：`cpymo_interpreter.c` 的 `D("chara")` 分支里依次弹 `chara_id_or_time / filename / pos_x / layer` 四段）。编号是整数，同一个编号再写一次就替换掉那张立绘；层号越大越靠前；坐标模式在这里**固定为 5**（水平居中、脚踩底边），想让立绘站别的位置用 `#chara_pos 编号,x,y,cm编号`。

`#chara` 是**一行一组**的收集命令：`编号,资源名,x 百分比,层` 四个逗号格一组，第 5 格起再读的那一个数字是**淡入时长（毫秒）**，读完这一行就结束。所以：

* `#chara 1,A,50,0` —— 摆一张，瞬现（时长 0）。
* `#chara 1,A,50,0,300` —— 摆一张，300 ms 淡入。
* `#chara 1,A,50,0,2,B,30,1,300` —— 一次摆两张，都 300 ms 淡入（四格一组连写，最后一格给时长）。
* `#chara 300` —— 这种只有数字的独立行**什么都不做**（解析出时长，可是没有立绘可摆）。老教程里「再补一行数字收尾」的说法在 CPyMO 里不成立，别照抄。

**引擎**：`cpymo_interpreter.c` 的 `D("chara")` 中 `POP_ARG` 只从当前行取格（`cpymo_parser_curline_pop_commacell` 不跨行），当后三格全空时它把首格当毫秒数然后 `break`。

`chara_multi`、`chara_pos`、`chara_y`、`chara_cls` 这些 `*_multi` 族同理：PyMO 文本里是「一行一组 + 末格时长」，只有 YukimiScript 侧才拆成「收集行 + `*_do` 收尾行」两条命令（参数见 [CPyMO 指令参考.md](CPyMO%20指令参考.md)）。

`#bg` 的参数顺序是 **`资源名,转场,时间,x,y`**（**引擎**：`D("bg")` 分支）。转场省略时默认 `BG_ALPHA`；时间可以写毫秒数，也可以写 `BG_VERYFAST`（100ms）、`BG_FAST`（175ms）、`BG_NORMAL`（250ms）、`BG_SLOW`（500ms）、`BG_VERYSLOW`（1000ms），省略时 300ms。`x`/`y` 是像素偏移，省略为 0。

`BG_NOFADE` 是要写在**转场位**上的符号，不是「不要转场」的时间开关；想要瞬切就连时间一起省，让它走默认 300ms 淡入，或者写 `BG_NOFADE` + `BG_VERYFAST` 把时长压到 0.1 秒。

`#change start` 的意思不是「跳转到 label start」，而是**重新载入脚本 `start`**。在这里它的作用是让对话结束后回到开头。真正的同脚本跳转用 `#goto 标签名`。

### 4.4 跑起来

桌面平台（SDL2 后端）：

```bash
cd cpymo-backends/sdl2
# 或按 README 用 CMake / nmake 构建
./cpymo /path/to/MyGame
```

没有图形环境时可以用 ascii-art 后端做纯逻辑验证：

```bash
cd cpymo-backends/ascii-art
make -j2
./cpymo-ascii-art /path/to/MyGame
```

按键：`W` `A` `S` `D` 移动选择，空格或 `J` 确认，`K` 取消，`L` 快进。

### 4.5 逐条验证清单

一个「能跑」不等于「对」。新写完一段脚本，至少确认：

1. 标题栏/标题画面显示的是 `gametitle`，没有乱码 —— 乱码是字体问题（见 7.3）。
2. 背景铺满整个逻辑画面，没有被拉伸变形 —— 素材比例和 `imagesize` 不一致时会被拉伸。
3. 立绘站在画面里而不是半个身子在屏幕外 —— 位置和 `cm` 模式是否配错。
4. `#se` 有声音但不会盖住 BGM。
5. 对话结束后 `#change start` 能回到开头，存档再读档能回到同一句。

---

### 4.6 跳转、子脚本与随机数

四条「换地方继续执行」的命令，语义差别很大，混用是最常见的 bug 来源：

| 命令 | 作用范围 | 会回来吗 |
| --- | --- | --- |
| `#goto 标签` | 只在**当前脚本文件**内找标签 | — |
| `#call 脚本名` | 切到 `script/脚本名.txt`，从头开始跑 | `#ret` 或子脚本跑完时回到 `#call` 的下一行 |
| `#change 脚本名` | 用另一个脚本**替换**当前脚本 | 不回当前脚本；但调用链保留，新脚本里 `#ret` 仍回到最初的调用者 |
| `#ret` | — | 回调用者；在顶层脚本里等于结束游戏 |

**引擎**：`#goto` 走 `cpymo_interpreter_goto_label`，只在当前脚本里查标签；`#call` 新建一个 interpreter 并把当前这个记为它的 `caller`；`#change` 换掉当前 interpreter 的 script，把原来的 `caller` 原样传给新脚本；`#ret` 在 `caller == NULL` 时返回 `CPYMO_ERR_NO_MORE_CONTENT`，也就是「没内容了」。

要点：

* `#call` 的参数是**脚本文件名**（`script/` 下，不带 `.txt`），不是标签。想把共用桥段抽出来，就单独建 `script/common.txt`，然后 `#call common`。
* `#goto` 跳不到别的脚本，跨脚本只能用 `#call` / `#change`。
* `#change` 看着像「换剧本」，实际是「当前脚本演完了，接着演另一个」。因为 `caller` 被保留，所以从子脚本里 `#change` 之后仍然能 `#ret` 回去——这就是它和「先 `#ret` 再 `#call`」的区别。

**条件分支**

`#if 条件,子命令` —— **第一个逗号之前是完整条件**，逗号之后是子命令名，子命令自己的参数照常用逗号跟在后面：

```
#if FSEL=0,goto NEW_GAME
#if S02>2,bg BG004,BG_FAST
```

* 运算符只有 `=`、`!=`、`<>`、`>`、`>=`、`<`、`<=`（**引擎**：`D("if")` 里逐个 `cpymo_str_equals_str` 比；写成别的样子会打 `[Error] Bad if expression "..." in script %s(%u).`）。
* 两侧可以是常量，也可以是变量名。**右侧写了不存在的变量时整行被静默跳过**（`cpymo_vars_access` 拿到 NULL 就直接 `CONT_NEXTLINE`），不报错——「条件好像没生效」多半是这个。
* 子命令只取逗号之后的第一个词，所以能内联的只有单条命令；要连着做好几件事就 `#if 条件,goto 标签`，把活儿放在标签下面。

**随机数**

`#rand 变量,最小值,最大值` 是**闭区间**：`#rand S,1,3` 的结果可能是 1、2、3。

**引擎**：`D("rand")` 先比 `max` 和 `min`，`max - min <= 0` 时直接报 `[Error] In script %s(%u), max value must bigger than min value for rand command.` 并返回 `CPYMO_ERR_INVALID_ARG`——**上下界不能相等**，想要固定值就别用 `#rand`。

## 5. 三个最常写的范例

### 5.1 标题画面 + 选项

```
#label START
#bg title,BG_NOFADE,BG_VERYFAST
#select_text 4,开始,继续,鉴赏,结束,3,67,50,100,#3E8BF4,1
#if FSEL=0,goto NEW_GAME
#if FSEL=1,goto LOAD_GAME
#if FSEL=2,goto ALBUM
#if FSEL=3,goto QUIT
```

**引擎**：`#select_text` 一条命令吃下所有参数，顺序是

```
数量,选项1..选项N,x1,y1,x2,y2,颜色,初始选中序号
```

返回值写进变量 **`FSEL`**，从 0 开始（**引擎**：`cpymo_select_img.c` 里 `cpymo_vars_set(..., "FSEL", sel)`）。所以第一个选项对应 `FSEL=0`。

四个坐标 `x1,y1,x2,y2` 是**百分比**：`x1,y1` 是文本框左上、`x2,y2` 是右下。横向把每行文字在 `[x1,x2]` 里居中，纵向把整块文字在 `[y1,y2]` 里居中（**引擎**：`cpymo_select_img.c` 的 `configuare_end_select_text`，`box_w = x2 - x1`、每行 `x = (box_w - w)/2 + x1`，最后统一加 `(y2 - y)/2`）。示例里的 `3,67,50,100` 读作「下半屏、左半边那块居中区域」。

颜色位照 3.2 写 `#RRGGBB`，写颜色名会得到品红。

手册里的这个例子和引擎行为一致，可以直接用。

### 5.2 多行选择支

```
#label START
#bg title
#sel 3
去学校
去海边
回家睡觉
#if FSEL=0,goto SCHOOL
#if FSEL=1,goto SEA
#if FSEL=2,goto SLEEP
```

`#sel N` 后面**紧跟 N 行**纯文本，每行一个选项。这是 PyMO 原版的写法，CPyMO 保留了（**引擎**：`D("sel")` 分支，注意它会把 `#sel 0` 这种旧写法排除掉）。

`#sel` 与 `#select_text` 的区别：`#sel` 用系统图片当选项底（`system/option.png`、`system/sel_highlight.png`），`#select_text` 用纯文字画选项。做新游戏建议 `#select_text`，样式更容易统一。

### 5.3 全局变量：每次进游戏随机一张背景

**`S` 开头的变量是全局变量**，存 `global.csav`，读档不影响它（**引擎**：全局变量进 `e->vars.globals`，与存档分开写）。这正好用来记「本局已经随机过没有」：

```
#label START
#if S02=0,goto RAND_BG
#if S02=1,goto BG2
#if S02=2,goto BG3
#if S02=3,goto BG4

#label RAND_BG
#rand S02,1,3
#if S02=1,goto BG2
#if S02=2,goto BG3
#goto BG4

#label BG2
#bg BG002
#goto TITLE
#label BG3
#bg BG003
#goto TITLE
#label BG4
#bg BG004
#goto TITLE

#label TITLE
#select_text 2,开始,结束,3,67,50,100,#FFFFFF,0
#if FSEL=0,goto NEW_GAME
#change start
```

要重置随机结果，在「结束」分支里写 `#set S02,0`。

注意 `#rand` 的上下界是**闭区间**：`#rand S02,1,3` 可能得到 1、2、3。

---

## 6. 鉴赏系统（CG 与音乐）

鉴赏系统由两套东西组成：一套是你的资源，一套是「列表脚本」。

### 6.1 哪些图会进 CG 鉴赏

**引擎**用 `cgprefix`（默认 `EV_`）判断：`bg` 与 `chara` 命令加载的资源名若以这个前缀开头（**不区分大小写**），就会被标记为「可解锁 CG」（**引擎**：`cpymo_album_cg_unlock`，调用点在 `cpymo_interpreter.c` 的 `bg` 与 `chara` 分支）。所以：

* 普通背景叫 `BG001`，永远不进鉴赏。
* 想进鉴赏的图命名成 `EV_001`，并在 `gameconfig.txt` 里保留 `cgprefix,EV_`（或改成你自己的前缀）。

解锁的时机就是「玩家真的看到它」。没看过的 CG 在鉴赏里显示占位图 `system/cvThumb.png`。

注意**解锁标记和鉴赏读图不是同一个目录**：`bg` 与 `chara` 两条命令都能把带前缀的资源标记成 CG，但鉴赏页取图时走的是 `cpymo_assetloader_load_bg_pixels`，也就是只从 `bg/` 里找（**引擎**：`cpymo_album.c` 的缩略图加载与大图加载两处）。所以真想让人在鉴赏里看到的图**必须放在 `bg/`**；放进 `chara/` 只会被标记，进鉴赏时找不到图。

### 6.2 album_list.txt

在 `script/` 下建 `album_list.txt`（**它是脚本目录里的资源脚本，不是纯文本清单**），一行一页：

```
页码,缩略图数,页名,文件名1,文件名2,...
```

* **引擎**只取 `页码 == 当前页 + 1` 的行（**引擎**：`cpymo_album.c` 的列表解析），所以页码从 1 开始。
* 中间那个「页名」字段被引擎丢弃，随便写。
* 每页最多 25 张（网格 5 列 × 5 行）。
* 行尾追加一个 `,1` 表示这一页不需要解锁、直接显示。

例子（第 1 页 3 张，第 2 页 2 张）：

```
1,3,第一页,EV_001,EV_002,EV_003
2,2,第二页,EV_004,EV_005
```

缩略图网格位置由引擎算：5 列，缩略图宽高为逻辑画面的 0.17 倍，左上角在 `(0.03 + 0.19 × 列, 0.02 + 0.19 × 行)`（**引擎**：`cpymo_album.c`）。你不需要自己排位置，但需要准备 5 张背景：`system/albumbg_0.png`、`albumbg_1.png`……从 0 编号。**注意这里有两套编号**：`album_list.txt` 的行页码从 1 开始，而背景图 `<前缀>_<页码>.png` 的页码从 0 开始，`albumbg_0.png` 对应列表里页码写 1 的那一页（**引擎**：`cpymo_album_load_ui_image` 直接用当前页号 `page` 拼文件名）。

### 6.3 进入鉴赏

```
#album
```

不带参数时用 `album_list` 作为列表脚本、用 `albumbg` 作为背景图名前缀（**引擎**：`cpymo_interpreter.c` 的 `D("album")`，空参时 `list_name="album_list"`、`ui_name="albumbg"`）。带参数时参数就是列表名：

```
#album album2
```

此时背景图前缀也跟着变成 `album2`，也就是说你需要 `system/album2_0.png` 这类文件。这是 PyMO 0.9 起才有的能力。

### 6.4 音乐鉴赏

```
#music
```

`#music` **不接受参数**（**引擎**：直接调 `cpymo_music_box_enter`），列表固定读 `music_list`。在 `script/` 下建 `music_list.txt`，一行一首、两个逗号格：

```
BGM001,片头曲
BGM002,夏日回忆
```

第一格是 `bgm/` 下的文件名（不带扩展名），第二格是列表上显示的文字。

**音乐鉴赏没有解锁判定**（**引擎**：`cpymo_music_box.c` 解析时完全不读任何 flag，`grep unlock` 在该文件里零命中），表里写几首就显示几首。这一点和 CG 鉴赏不同——只有 album 才有「看过才亮、否则显示 `cvThumb.png`」。

### 6.5 缩略图缓存

第一次进鉴赏要现场缩放所有 CG，会明显卡一下。用工具预生成缓存：

```bash
cpymo-tool gen-album-cache /path/to/MyGame
```

它把缓存写进游戏目录的 `system/`，文件名是 `<底图前缀>_<页码>.png`，页码从 0 起（与运行时一致；工具内部从列表页码 1 开始遍历、传给生成函数时减一）。它会把分析器找到的**所有** album 列表都过一遍，不只是 `album_list`：非默认列表的前缀就是那个列表的名字（**引擎**：`cpymo/cpymo_album.c` 的 `gen-album-cache` 分支用 `stbi_write_png` 落盘），之后再进鉴赏就直接加载这些图、不再现场缩放。删掉它们就退回现场缩放：那种兜底画法只在内存里存在，**不落盘**。

---

## 7. 素材、字体与后端

### 7.1 推荐的素材规格（手册给的）

老手册给的参数偏保守，但作为「什么格式最保险」的参考仍然有效：

| 素材 | 格式 | 建议 |
| --- | --- | --- |
| 背景 | JPG | 640×480 或与你 `imagesize` 同比例 |
| 立绘 | PNG（256 索引色或 RGBA） | 透明区域用 alpha，不要用纯色抠图 |
| 立绘遮罩 | PNG 灰度图 | 与立绘同尺寸；**只有 `platform` 以 `s60v` 开头（不分大小写）时**才按 `charamaskformat` 去找 `_mask` 图 |
| BGM | MP3 44100 Hz / 192 Kbps / 立体声 | — |
| SE、语音 | MP3 或 WAV 22050 Hz / 48 Kbps / 单声道 | 语音一定单声道 |

手册里 S60v3 用的 320×240、S60v5 用的 540×360，那是诺基亚时代的屏。做现代平台直接用 1280×720 起。

**遮罩的语义**（塞班时代遗物）：`A.png` 的遮罩是 `A_mask.png`，同尺寸的 256 级灰度图，**越黑越透明、越白越不透明**。Android 与 PC 版不用遮罩，直接用带 alpha 的 png。老手册提到的 `pymo_mask_to_png` 工具 CPyMO 仓库里没有，移植老素材要自己把「图 + 灰度遮罩」合成成 RGBA png（ImageMagick 里 `compose CopyOpacity` 一类操作即可）。

### 7.2 系统图片清单

`system/` 下这些名字是引擎**直接按名字找**的（**引擎**：`cpymo_assetloader_load_system_image` 的各调用点）。下面这张表里的名字写死在 C 代码里（唯一例外是 `albumbg_N` 的前缀），而且**一张都不给也能跑**——缺图时引擎要么跳过、要么用内置画法凑合。另有几类图名由脚本命令的参数决定，见本节后半「跟着脚本参数走的系统图」：

| 文件 | 谁在用 |
| --- | --- |
| `menu.png` | 菜单背景（`cpymo_rmenu.c`） |
| `option.png` | `#sel` / `select_img` 的选项底图（`cpymo_select_img.c`） |
| `sel_highlight.png` | 选中高亮条（同上） |
| `message_cursor.png` | 打字光标，约 24×24（`cpymo_say.c`） |
| `albumbg_0.png`、`albumbg_1.png`… | 鉴赏每页底图，从 0 编号；**前缀跟着 `#album` 的参数走**（见 6.3，写 `#album album2` 就是 `album2_0.png`）。找不到时引擎用内置画法当场画一张凑合（`cpymo_album_generate_album_ui_image`），只在内存里用，不落盘 |
| `cvThumb.png` | 未解锁 CG 的占位缩略图 |

对话框底图（老游戏里叫 `message.png`）和名字框底图（`name.png`）**不是固定名字**：它们由 `#textbox <对话框图>,<名字框图>` 命令按名字指定，你想叫什么都可以。压根不调 `#textbox` 时，引擎用内置的画法，一个图都不需要（**引擎**：`cpymo_interpreter.c` 的 `D("textbox")` 把两个名字直接传给 `cpymo_say_load_msgbox_and_namebox_image`）。

这些底图配一张 `<图名>_mask.png`（对话底图与名字框底图都在 `system/`）是老 PyMO 的约定，但 **CPyMO 只在 `platform` 以 `s60v` 开头（不分大小写）时才会加载 `_mask`**（与 2.2 的 `charamaskformat` 同一个门控），其它平台一律忽略。新游戏直接用带透明通道的 png 就行。

只做新游戏时，这些图都可以先用纯色块顶替，跑通流程再换美术。

**跟着脚本参数走的系统图**

除上表的固定名字，还有五类图也在 `system/` 里，但名字由命令参数给，写什么就找什么（都走 `cpymo_assetloader_load_system_image`，png）：

* **`#bg` 的转场图**：`#bg <背景图>,<转场图>` 的第二个参数若不是空、也不是 `BG_NOFADE` / `BG_ALPHA` / `BG_FADE` 这三个关键字，就按这个名字找 `system/<转场图>.png`。
* **鉴赏底图** `albumbg_N`：前缀跟着 `#album` 的参数走（见 6.3）。
* **`#date` 的日期背景**：`#date <底图名>,<x>,<y>,<颜色>`，第一个参数**必填**（引擎里是 `ENSURE(date_bg)`），直接按这个名字找 `system/<底图名>.png`。显示的月/日取自变量 `FMONTH` / `FDATE`，没设过就是 0。
* **`#anime_on` 的动画图**：`#anime_on <帧数>,<图名>,<x>,<y>,<每帧毫秒>,<是否循环>`。`system/<图名>.png` 是**一张竖排拼图**（spritesheet），引擎按「图片高度 ÷ 帧数」切成等高帧依次播放，**不是多张序列图**；`LOW_FRAME_RATE` 构建下这条命令整条跳过。
* **选项提示图**：只有 `#sel`（和 `sel_do`）收这个参数——`#sel <选项数>,<提示图前缀>`。引擎找 `system/<前缀>0.png` 到 `<前缀>3.png` 四张，**从 0 起顺序加载，缺一张就停在那里**。真正画哪两张由选项文本的**首字符**决定（**引擎**：`cpymo_interpreter.c` 的 `D("sel")`）：`○`(U+25CB) 用 0/1，`×`(U+00D7) 用 2/3，其它首字符给了提示图也不画；同一组两张交替闪烁，分别当「未选中 / 选中」。所以要么四张都给，要么只给 0/1 并保证这些选项都以 `○` 开头。`gameconfig` 的 `hint` 写 0 时一张都不读（`cpymo_select_img_configuare_select_text_hint_pic` 开头就 return）。

`#select_text` / `#select_var` / `select_img` 族**没有**提示图参数，hint 恒为 nohint。

**游戏图标不在 `system/` 下**：它是游戏根目录的 `<游戏目录>/icon.png`，与 `gameconfig.txt` 同级——老手册的两处写法（目录树里的 `icon.png 游戏图标`、6.7 节的「必须为 57×57」）指的都是这个文件。它不参与任何脚本命令，引擎在三处用到它（**引擎**：`cpymo/cpymo_assetloader.c` 的 `cpymo_assetloader_load_icon_pixels`，按 `<gamedir>/icon.png` 读）：

- **桌面窗口图标**：SDL2 后端启动时调 `set_window_icon`（`cpymo-backends/sdl2/main.c`），只在 Windows / Linux / macOS 上做，加载失败静默跳过；SDL1 后端另有一份读图代码，由 `-DLOAD_GAME_ICON` 打开（`cpymo-backends/sdl1/main.c`，读进来会缩到 32×32）。
- **游戏选择器缩略图**：编进 `USE_GAME_SELECTOR` 时，列表里每款游戏显示这个图，加载失败就不显示（`cpymo/cpymo_game_selector.c`）。
- **工具链复制**：`strip` 与 `convert` 无条件把它复制到输出目录，失败只打印 `[Warning] Can not copy icon.png`。

57 是老塞班时代定的尺寸：**不合规只打印 `[Warning] Icon must be 57x57`，游戏照跑**；整个文件不提供也不影响启动，只是没有窗口图标和列表缩略图。它是 png，不受任何 `*format` 键影响，也不会被打进 pak。

### 7.3 字体

字体不是 `gameconfig.txt` 管的，是**后端**在启动时加载的。桌面 SDL2 构建里有两份互斥的实现，靠宏二选一：

* 默认是 `cpymo-backends/sdl2/cpymo_backend_font.c`（stb_truetype，整体裹在 `#ifndef DISABLE_STB_TRUETYPE` 里）；
* 定义 `ENABLE_SDL2_TTF` 时换成 `cpymo-backends/sdl2/cpymo_backend_text_sdl2_ttf.c`（走 SDL_ttf），候选表里多一个 `.fnt`。

`cpymo-backends/sdl2/CMakeLists.txt` 用 `file(GLOB ...)` 收下目录里所有 `.c`，所以两份文件都在编译单元里，选哪份完全看宏——不要同时开两个，它们定义的是同一组 `cpymo_backend_font_*` 符号。

stb_truetype 那份的查找顺序（**引擎**：`cpymo_backend_font.c` 的 `cpymo_backend_font_init`）：

1. `<游戏目录>/system/default.ttf`
2. `<游戏目录>/system/default.otf`
3. 编译期宏 `GAME_SELECTOR_DIR` 下的 `default.ttf` / `default.otf`（游戏选择器和掌机版用；不定义就没有这一步）
4. 编译期宏 `GAME_SELECTOR_DIR_2` 下同样两个名字（只有 PSV 定义，指向 `ur0:/pymogames/`）
5. 平台回退：Windows 扫 `%windir%\fonts` 里一串中文字体（`msyh.ttc`、`meiryo.ttc`、`mingliub.ttc` 等）；Linux 试 `/usr/share/fonts/opentype/noto/` 下 `NotoSansCJK-Regular.ttc`、`NotoSerifCJK-Regular.ttc`、`NotoSansCJK-Bold.ttc`、`NotoSerifCJK-Bold.ttc` 和 `/usr/share/fonts/truetype/unifont/unifont.ttf`；macOS 读 `STHeiti Medium.ttc` / `STHeiti Light.ttc`；iOS 读应用文档目录；Switch 借系统共享字体（跳过 0 号）

`GAME_SELECTOR_DIR` 在构建脚本里值各不相同：Android 是 `/sdcard/pymogames`（`cpymo-backends/android/app/src/main/cpp/CMakeLists.txt`）、Switch 是 `sdmc:/pymogames/`、PSP 是 `./`、Wii 是 `sd:/pymogames/`、PSV 是 `ux0:/pymogames/`。指南前面几处提到的 `/sdcard/pymogames/default.ttf` 就是 Android 上的这一条，**不是**什么平台回退分支。

**Android 没有系统字体可借**：上面五步全落空时只弹一句 `Can not find /sdcard/pymogames/default.ttf`，随后 `main.c:626` 打印 `[Error] Can not find font file, try put default.ttf into <gamedir>/system/.` 并退出。所以在 Android 上字体是必需品，要么塞进游戏目录的 `system/`，要么放一份到 `/sdcard/pymogames/`。

所以**把字体放进游戏目录的 `system/default.ttf`** 是最省事的做法，游戏跟着目录走，换机器不会乱码。

其他平台的约定位（README 各平台章节）：

* 3DS：`sdmc:/pymogames/font.bcfnt` 优先，找不到用内置字体。
* Switch：`sdmc:/pymogames/default.ttf`；这条也没有时会借系统共享字体，通常仍能显示。
* PSP：`PSP/GAME/CPYMO/default.ttf`，建议小于 2MB。
* iOS：应用文档目录下 `default.ttf`。

中文字体动辄十几 MB。要控制体积，先用 `cpymo-tool strip` 裁掉不用的资源，或换一个子集字体。

### 7.4 后端差异会咬人的地方

| 事情 | 哪些后端支持 |
| --- | --- |
| 影片（`movie`） | 解码在引擎侧：`cpymo/cpymo_movie.c` 直接调 libavformat/libavcodec，桌面 CMake 是 `find_package(FFMPEG REQUIRED)`。要不要影片只看构建里的 `DISABLE_MOVIE`，与图形后端无关。**固定没有视频**：ascii-art、text、PSP、PSV、Wii（`sdl1/Makefile.Wii:29`）、Windows 的 `sdl2/Makefile.Win32`（`:39`）。**默认有、但在某些音频选项下被关掉**：`sdl2/Makefile` 与 `sdl1/Makefile`（加 `DISABLE_AUDIO=1`、或改用 `USE_SDL2_MIXER=1` / `USE_SDL_MIXER=1` 音频时就带上 `-DDISABLE_MOVIE`）、Emscripten（只有 `AUDIO_BACKEND=ffmpeg` 才有影片）。**有视频**：桌面与 iOS 的 CMake、Switch、3DS、libretro。片源扩展名写死在引擎里：`#movie <名>` 只找 `video/<名>.mp4`，参数别自带 `.mp4`。`gameconfig` 的 `playvideo` 写 `0` 时这条命令静默跳过——跳过前会先把 bgm、se、voice 停掉，只留一句 `[Info] Playvideo is disabled in gameconfig.` |
| 文本输入（`input`） | 只有实现了 SDL_TEXTINPUT 的后端 |
| 立绘遮罩（`_mask`） | 与后端无关，只跟 `platform` 有关（见 7.2） |
| 语音/音效 | 需要有音频后端，ascii-art 后端没有 |

在 ascii-art 后端上测游戏时，凡是涉及画面和声音的命令都只能验证「不崩、不报错」，**不能验证效果**。这是环境限制，不是 bug。

### 7.5 怎么把游戏跑起来

桌面与终端后端（SDL2 的 CMake / Makefile、SDL1、ascii-art、text）读命令行参数，但**只认正好一个**：`argc == 2` 时才用 `argv[1]`，一个不给、或者给多了，都回落到当前目录（`sdl2/main.c:464-470`、`ascii-art/main.c:106-108`、`text/main.c:178-181`）。

```bash
cpymo-sdl2      /path/to/MyGame   # SDL2 桌面版
cpymo-ascii-art /path/to/MyGame   # 纯文本后端
```

定义了 `USE_GAME_SELECTOR` 的构建（PSP、PSV、Wii、Switch、Android、iOS）**完全不看 `argv`**：启动后在自己的固定目录里列出游戏，选中了才进。Emscripten 版同样没有参数可言，游戏目录是编译期常量 `EMSCRIPTEN_GAMEDIR`。所以对平台版来说，「把游戏放到哪个目录」是唯一要紧的事，见下面列表。

ascii-art 后端在 `cpymo-backends/ascii-art/`，`make LUA=1` 就能产出 `cpymo-ascii-art`。它把画面画成字符网格，所以只能验脚本流程、变量、分支和「不崩」，验不了美术效果（原因见 7.4）。按键：`W/S/A/D` 移动，空格或 `J` 确认，`K` 取消，`L` 跳过（后端对输入做了 `toupper`，小写一样管用）。

平台版的游戏目录位置由各平台自己约定，多半在存储卡的固定位置，构建/部署细节见 README 的对应章节：

- 3DS：SD 卡的 `/pymogames/` 下，游戏目录名只能用半角英文、数字和下划线（README 的「3DS」节）
- Switch：`sdmc:/pymogames/`
- PSV：`ux0:/pymogames`
- Wii：`sd:/pymogames/`
- PSP：`PSP/GAME/CPYMO/`，并且资源必须先过一遍 `cpymo-tool convert psp`
- Android：`/sdcard/pymogames/`
- iOS：iTunes 文件共享的文档目录

---

## 8. 打包与发布：cpymo-tool

`cpymo-tool` 是仓库自带的资源工具，所有子命令都在 `cpymo-tool/main.c` 的帮助文本里。构建后直接跑 `cpymo-tool` 会打印用法。

### 8.1 子命令一览

| 子命令 | 用法 |
| --- | --- |
| `unpack` | `unpack <pak> <扩展名带点> <输出目录>`，拆包 |
| `pack` | `pack <输出.pak> <文件...>` 或 `pack <输出.pak> --file-list <list.txt>`，打包 |
| `resize-image` | `resize-image <源> <目标> <比例宽> <比例高> [--load-mask [格式]] [--create-mask [格式]] [--out-format png/bmp/jpg]`。两个比例是**倍数**不是像素：`1` 原尺寸、`2` 放大一倍、`0.5` 减半 |
| `pack-spritesheet` | `pack-spritesheet <输出> <列数> <文件...> [--load-mask] [--create-mask] [--out-format]` |
| `gen-album-cache` | `gen-album-cache <游戏目录>`，生成鉴赏缩略图缓存 |
| `strip` | `strip <游戏目录> <输出目录> [--pack]`，剔除未引用的资源 |
| `convert` | `convert <目标平台> <游戏目录> <输出目录> [--pack]`，一把梭：缩放图片、改配置、可选打包 |

**`strip` 做的是裁剪，不是压缩。** 它从 `gameconfig` 的 `startscript` 出发解析脚本，遇到 `#call` / `#change` 跟到被调用的脚本（`#goto` 只是同脚本内跳转，不用跟），把真正被引用到的资源名收成集合，然后只复制这些文件：

* 七类资源各扫各的：`bg`、`bgm`、`chara`、`se`、`voice`、`video`、`script`。`#bg xxx BG_FADE` 这种第二参数是转场名的写法它认得，不会把 `BG_FADE` 当图片名。
* 系统图里有三张是写死无条件复制的：`system/menu`、`system/message`、`system/name`；`bg/logo1`、`bg/logo2` 同样无条件保留，哪怕脚本一次都没提过（`cpymo_tool_asset_analyzer.c:155-166`）。另有四张看脚本用没用：`sel_highlight`（`#say`、`#sel`、`#select_text` / `#select_var` 触发）、`message_cursor`（`#say`、`#text`）、`option`（`#sel`）、`cvThumb`（`#album`，不生成遮罩）。`#music` 出现时才保留 `script/music_list.txt`——它在 `script/` 下，不是系统图。`icon.png` 与 `system/default.ttf` 无条件复制。
* `gameconfig.txt` 无条件复制，**内容一字不改**。
* 不生成任何 `_mask` 遮罩——遮罩只有 `convert` 转塞班时才有。
* 用变量拼出来的资源名（`set A,bg01` 之后 `#bg ${A}`）它认不出来，这类资源会被当成没引用而丢掉。发布前先备份。

**`--pack` 只对四类资源生效。** 过滤器里每类资源带一个 `packable` 开关：`bg`、`chara`、`se`、`voice` 可打包，`bgm`、`video`、`script`、`system` 永远是散文件。所以 CPyMO 里没有「全打成包」这回事，`bgm/` 那一堆文件始终留在磁盘上。

* 包位置固定为 `<输出目录>/<类型>/<类型>.pak`（`bg/bg.pak`、`chara/chara.pak`……），不能自定义包名。
* 包内条目名取文件**基名、去掉扩展名、转大写**，最长 31 字符，超了打印 `[Warning] File name ... is too long!`。pak 里不存扩展名，后缀由 `gameconfig` 的 `*format` 决定——这也是为什么换格式要改配置而不是改包。
* `unpack <pak> <扩展名> <输出目录>` 反过来：条目名转小写，直接接上你给的字符串，**必须自己带点**（`.jpg`），它不做任何补全。
* `pack <输出.pak> --file-list <列表>` 的列表是**一行一个磁盘路径**（`/`、`\` 都行），空行跳过、每行首尾空白会 trim；条目名照样只看基名，所以 pak 里没有目录层级，拆出来是一个平铺目录。

不带 `--pack` 时，`strip` 与 `convert` 保持输入的形态：源目录散着就给散文件，源目录有 pak 就写回 pak。

### 8.2 convert 的目标平台规格

`convert` 按内置表改写图片尺寸与 `gameconfig.txt`（**引擎**：`cpymo_tool_convert.c` 的 `cpymo_tool_convert_specs` 表）。下面每一列都能在代码里找到出处：

| 目标 | 参考分辨率 | 生成立绘遮罩 | 强制音频转码 | 缩放取 | bg / 立绘 / 系统图 / 遮罩 | 写入的 platform |
| --- | --- | --- | --- | --- | --- | --- |
| `s60v3` | 320×240 | 是 | 是 | 小比例 | jpg / jpg / png / jpg | `s60v3` |
| `s60v5` | 540×360 | 是 | 是 | 小比例 | jpg / jpg / png / jpg | `s60v5` |
| `3ds` | 400×240 | 否 | 否 | 大比例 | jpg / png / png / 不生成 | `pygame` |
| `pymo` | 800×600 | 否 | 否 | 小比例 | jpg / png / png / 不生成 | `pygame` |
| `psp` | 480×272 | 否 | 是 | 小比例 | jpg / png / png / 不生成 | `pygame` |
| `wii` | 640×480 | 否 | 是 | 小比例 | jpg / png / png / 不生成 | `pygame` |

**缩放只有一套比例，不是逐图适配。** 比例由 `gameconfig` 的 `imagesize` 与目标分辨率算出：`3ds` 取两个方向里的**大**比例（铺满屏幕，超出的部分看不到），其余目标取**小**比例（整张装进屏幕，可能留边）。bg、立绘、系统图共用这一个比例，所以 1000×200 的长条背景和小图标会被同倍数缩放。`imagesize` 本身也会乘上同一比例写回——800×600 转 `3ds` 时 max(400/800, 240/600) = 0.5，结果是 400×300，不是 400×240。

几点值得注意：

* **系统图一律重存为 png**，与目标无关；立绘遮罩只有 `s60v3` / `s60v5` 生成，格式 jpg。
* `3ds` 铺满意味着竖构图的立绘会超出上下边界，移植前先决定要不要自己重做素材。
* 除了 `s60v3` / `s60v5`，其他目标把 `platform` 写成 `pygame`。`platform` 在 CPyMO 里只有两个用途：塞班遮罩门控，以及 `cpymohd1/2/3` 这三个值用来选 feature level（见 2.2）。**`convert` 会把输出目录的 `platform` 覆盖成上表的值**，所以转换完要手工改回，否则 Level 1 的脚本跑不起来。
* **`playvideo` 转换后照抄原值。** 规格表里那个 `play_video` 字段从没被读取过——`convert` 里唯一提到它的一行是 `if (!cfg.playvideo) cfg.playvideo = false;`，等于什么都没干。想让掌机不播影片，得自己把配置改成 `playvideo,0`。
* **非塞班目标的立绘遮罩格式是空的。** 表里 `charamaskformat` 只有 `s60v3` / `s60v5` 有值，其余目标是 `NULL`，写回时被清成空串，于是输出的 `gameconfig.txt` 里会出现一行 `charamaskformat,.`（只有一个点）。这不是工具出错，非塞班平台本来也不读它。
* PSP 的游戏数据包必须用 `convert psp` 转换过才能跑（README 的 PSP 章节明确要求）。
* `convert` 会顺手把 `icon.png` 与 `system/default.ttf` 复制到输出目录（前者失败只警告）。

**`convert` 会把 `gameconfig.txt` 整份重写。** 它按引擎规则解析原配置，再按固定字段表写一份新的（`cpymo_tool_gameconfig_write_to_file`，顺序就是写文件的顺序）：`gametitle`、`platform`、`engineversion`、`scripttype`、六个格式键、`font`、`fontsize`、`fontaa`、`hint`、`prefetching`、`grayselected`、`playvideo`、`textspeed`、`bgmvolume`、`vovolume`、`imagesize`、`startscript`、`nameboxorig`、`cgprefix`、`textcolor`、`msgtb`、`msglr`、`namealign`。写死的几项：`scripttype` 一律写 `pymo`（4 字节字段只留 3 个字符，存进去还是 `pym`，与手写等价）、格式键带前导点（`.jpg`）、`font` 写 `-1`、`fontaa` 与 `prefetching` 写 `1`、`namealign` 写 `middle` / `left` / `right`、`textcolor` 写成小写 `#rrggbb`。`font`、`fontaa`、`prefetching` 这三个键 CPyMO 引擎不认（见 2.3），写了只是留给上游 PyMO。**原文件里的注释行、`#` 开头的行、引擎不认识的键，转换后全部消失**——要留着就转换后再手工补回。

**音频筛不过就转码。** `s60v3` / `s60v5` / `3ds` / `psp` 接受 OGG / MP3 / WAV，`pymo` 与 `wii` 只接受 OGG / WAV；`s60v3` / `s60v5` / `psp` / `wii` 的「强制音频转码」为真，即格式本来就支持也统一转。输出扩展按 OGG > MP3 > WAV 挑，而六个目标都支持 OGG，所以只要 ffmpeg 在，最终一律是 ogg。`bgm`、`se`、`voice` 各按自己的现有格式判断，转完写回 `bgmformat` / `seformat` / `voiceformat`。

**视频不看平台表。** 只要 ffmpeg 在，视频一律转 mp4（`-c:v mpeg4 -c:a aac`）并按同一比例缩放；规格表里那个「是否播影片」的字段在代码里没有被读取过，改不了任何东西。

**ffmpeg 是唯一的依赖，缺了也不报错。** 找不到 ffmpeg 时音频与视频原样复制，只打印 `[Warning] ffmpeg not found...`，而 `gameconfig` 里的格式键仍写成目标值——配置说 ogg、文件还是 mp3，游戏里就是没声音。转完务必核对配置与文件后缀。

老手册说 pymo 支持 `wav`/`mp3`/`amr`/`aac`/`midi`。`amr` 与 `aac` 任何 CPyMO 后端都解不了（SDL_mixer 没有这两个解码器）；`midi` 要看构建：SDL2 后端确实请求了 `MIX_INIT_MID`（`cpymo_backend_audio_sdl2_mixer.c:63-68`），但真能出声取决于 SDL_mixer 编译时带没带 timidity / fluidsynth，不能指望。移植别人的游戏时统一先转成 OGG 或 MP3 最省事——有 ffmpeg 的话 `convert` 会替你转，没有就自己先转。

### 8.3 一个完整的发布流程

```bash
# 1. 剔掉没引用的资源
cpymo-tool strip MyGame MyGame-stripped

# 2. 按目标平台转换
cpymo-tool convert 3ds MyGame-stripped MyGame-3ds --pack

# 3. 生成鉴赏缓存（如果用了鉴赏；就地写进 MyGame-3ds/system/）
cpymo-tool gen-album-cache MyGame-3ds

# 4. 把 MyGame-3ds 整个目录拷到目标设备的约定位置
```

`--pack` 会把 `bg`/`chara`/`se`/`voice` 打成 pak，`bgm` / `video` / `script` / `system` 仍是散文件。

---

## 9. 移植已有的游戏

这一节有两种情况：把**老 PyMO 游戏**搬到 CPyMO（脚本本来就是一种 CPyMO 认的方言），和把**别的引擎的游戏**（KiriKiri、Ren'Py、NScripter、RPG Maker、自研引擎……）搬到 CPyMO。前者主要是「修」，后者是「重新组织」，工作量差一个数量级，别混着想。

### 9.1 先判断属于哪一类

| 你手上的东西 | 属于哪类 | 从哪读 |
| --- | --- | --- |
| 已解密的 `#命令` 文本脚本 + 素材（PyMO 的产物） | 老 PyMO，可能直接就能跑 | 9.2 → 9.4 |
| PyMO 的 `.py` 脚本 + 素材 | 老 PyMO，脚本要人工翻成文本命令 | 9.2 → 9.4 |
| 别的引擎的封包 / 脚本 / 工程 | 换引擎，素材要提、脚本要重写 | 9.5 → 9.8 |
| 只有视频、漫画或小说，没有脚本 | 不算移植，是从零写 | 第 4 节 |

判据只有一条：**脚本是不是已经以 CPyMO 认识的命令流存在**。是，就在原有基础上修；不是，就得重新组织成命令流。CPyMO 没有「导入别的引擎脚本格式」的入口，也没打算有——它读的永远是 `script/*.txt` 里的 `#命令`（或 `ykmc` 编译出来的同种文件）。

### 9.2 老 PyMO：先看清你要移植的是什么

PyMO 游戏通常是两种形态之一：

1. **`.py` 脚本 + 素材**：脚本是 Python 代码，直接调用 PyMO 的 Python API。
2. **已解密的 `#命令` 文本脚本 + 素材**：已经被人从 `.py` 或打包文件里还原成文本。

形态 1 需要先把 Python 代码翻译成文本脚本（或翻译成 YukimiScript）。这一步没有通用工具，只能人工/半自动做：把 `py` 里的调用逐句替换成等价命令。手册第 2 章给的建议仍然正确：

> 批量处理、用正则或 Python 写脚本做替换，不要一句一句手改。

形态 2 可以直接跑，见 9.3。

### 9.3 老 PyMO：直接跑，先修四件事

把老游戏的目录结构对齐 CPyMO 的要求，通常只需要改这四处：

1. **`gameconfig.txt`**：老游戏可能缺 `scripttype`（补 `pym`）、缺 `imagesize`（补上原始分辨率）、`startscript` 名要和 `script/` 里的文件名对上。
2. **扩展名**：老游戏常见 `bgformat,.jpg`（带点）。CPyMO 两种都吃，不用改。
3. **编码**：脚本必须是 UTF-8。老素材若是 GBK/Shift-JIS，先转码再放进去，否则满屏乱码。
4. **文件名大小写**：资源名在 Linux/Android 上区分大小写，Windows 上不区分。老游戏在 Windows 上写的脚本，搬到手机常常报「找不到资源」就是这个原因。用 `strip` 或自己写脚本核对一遍引用名和实际文件名。

### 9.4 老 PyMO：跑起来之后逐条对差

老脚本能跑不等于行为一致。按这个顺序检查，越靠前的问题越常见：

| 症状 | 大概原因 | 怎么办 |
| --- | --- | --- |
| 立绘位置全错 | 坐标模式写错 | 见 3.4：PyMO 文本里必须写纯数字 `0`–`6`，写 `cm5` 会被 `atoi` 读成 0（跑到左上角）；单条 `#chara` 固定按 5 摆 |
| 选择支结果对不上 | 结果变量起点不同 | CPyMO 从 0 开始写 `FSEL`；老 PyMO 有些版本从 1 开始 |
| 鉴赏是空的 | 没有 `cgprefix` 前缀 | 见 6.1，把参战图改名或改 `cgprefix` |
| 鉴赏页数不对 | `album_list` 页码从 0 开始 | CPyMO 只认 `页码 = 当前页 + 1`，即从 1 开始 |
| 音乐鉴赏点了没反应 | `#music` 带了参数 | CPyMO 的 `#music` 不接受参数 |
| 打字速度/音量不对 | `save/config.csav` 覆盖了配置 | 删掉玩家的 `config.csav` 再试一次 |
| 满屏方框 | 字体缺字 | 见 7.3，把字体放进 `system/default.ttf` |
| 影片播不出来 | 后端没链 FFmpeg | 见 7.4 |
| 报「未知命令」但游戏能继续 | 该命令 CPyMO 没实现 | 引擎对未知命令只警告并跳过，不会崩；去 [CPyMO 指令参考.md](CPyMO%20指令参考.md) 查有没有替代写法 |
| 资源找不到但文件在 | 大小写或扩展名 | 大小写见 9.3 第 4 条；扩展名本身不用管——引擎是按 `*format` 自己拼的，扩展名写错只会在配置上体现 |

### 9.5 其他引擎：CPyMO 能承载什么

动手之前先确认原游戏用到的东西 CPyMO 有没有。**没有的**，遇到就得改设计，而不是改写法：

| 原引擎常见能力 | CPyMO 的现状 |
| --- | --- |
| 任意 UI 绘制（自定义按钮、自绘菜单、内嵌小游戏界面） | 没有即时 UI API。只有内置页面（对话框、存档、读档、设置、回想、鉴赏、音乐），Level 1 能用 `cpymo.ui` 改写这些页面的条目，但不能凭空画一块自由界面 |
| 多层立绘合成（多个部件各自移动、局部差分） | 每个立绘槽位一张整图，`chara/<名字>.<charaformat>`。差分层要在外部先烘成整图再给它 |
| 脚本里的函数、循环、数组、浮点 | 只有字符串变量、`#if` 的整数比较、`#goto`/`#call` 跳转。没有函数、没有循环、没有浮点 |
| 3D、着色器、粒子、物理 | 没有 |
| 原引擎自带的小游戏与系统界面 | 没有 |

**有的**，足够覆盖大多 AVG 需求：

* 画面：`#bg`、`#chara` 族、`#flash`、`#quake`、`#scroll`、`#chara_scale`、`#chara_scroll`、`#chara_anime`、`#filmmode`、`#avatar`
* 声音：`#bgm` / `#bgm_stop`、`#se` / `#se_stop`、`#vo`、`#wait_se`
* 文本：`#say`、`#text`、`#title`、`#title_dsp`、`#intro`、`#textbox`、`#text_off`
* 流程：`#goto`、`#call`、`#ret`、`#label`、`#if`、`#if_goto`、`#rand`、`#change`、`#wait`
* 选择：`select_text`、`select_var`、`select_img` 三个族（各有 `_do` 收尾行）
* 存档与鉴赏：`#load`、`#album`、`#music`、`#date`
* 变量输入：`#input`

**能做到，但得绕。** 下面这些在原引擎里往往是一行代码，在 CPyMO 里要用现成命令拼出来。清单按「绕法 → 代价」写，能接受代价再动手：

| 想要的效果 | CPyMO 里的绕法 | 代价 |
| --- | --- | --- |
| 函数 / 子程序 | `#call` 进一个脚本文件，`#ret` 回来 | 没有参数表与返回值，靠全局变量约定；最小单位是一个脚本文件，不能在一个文件里定义多个函数 |
| 循环 | 计数器变量 + `#if_goto` 跳回标签 | 没有 `break` / `continue`；次数必须能写成整数比较 |
| 数组 / 字典 | 展平成 `v0`、`v1`……，取值用 `#if` 链或 `#select_var` 分派 | 没有长度、没有遍历，每加一格多写一条命令 |
| 浮点 | 乘 100 或 1000 存整数 | 所有公式手工定标；`#chara_scale` 是唯一吃小数的命令 |
| 任意排版 | 一条 `#say` 只放一段；别处的文字用 `#text`（x1,y1,x2,y2 百分比 + 颜色 + 字号） | `#text` 不记入对话记录、不写存档，玩家回看不到 |
| 局部染色 / 注音 | 富文本标记 `[文字](style=color~#RRGGBB)`、`[汉字](ruby=…)`，`say` / `intro` / `text` 都能用 | 只支持这两种；键名或值写错就整段原样显示 |
| 自绘菜单 / 按钮 | 内置页面近似：选项用三个 `select_*` 族，图形式 UI 用 `select_img`，常驻文字用 `#text` 或 Level 1 的 `cpymo.ui.hud`（常驻文本 HUD） | 画不出真正的自由界面，按钮位置由菜单布局决定 |
| 多层立绘 / 局部差分 | 多个槽位各放一张整图，`chara_multi` / `chara_y_multi` 族一次摆好 | 差分层要在外部烘成整图；同一槽位不能叠图 |
| 立绘局部动画（眨眼、发丝） | `#chara_anime` 让整张图抖动，或 `#anime_on` 把 `system/` 下一张竖排拼图按帧数切开循环播放 | `#anime_on` 同屏只能一个、画在立绘之上，拼图要在外部先烘好；`#chara_anime` 移动的是整张图 |
| 定时器 / 并行 | `#wait` 阻塞等待；要并行只能上 Level 1 的 `cpymo.timer` / 协程 | 纯文本脚本没有并行 |
| 选项按条件出现 | `#select_var`，变量为 0 的项不显示 | 不显示的项仍占 `FSEL` 编号 |
| 已选项变灰、选择中存档 | `#select_var` 的 `init_position` 设 -1 | 只对 `sel` / `select_var` 有效 |
| 数字输入（密码、金额） | `#input 变量[,提示]` | 只收数字、最长 16 字符，没有文本输入 |
| CG / 音乐鉴赏 | `#album` + `album_list.txt`、`#music` | 音乐鉴赏没有解锁判定，列出来就能听；CG 的图只从 `bg/` 里加载（6.1） |
| 一次点击触发多处变化 | 逐条发命令：`#bg` 一条、`#chara` 一条、`#se` 一条 | 命令逐条执行，只有带时间的命令会阻塞 |

格式上的硬线在 7.4 与 8.2：图片格式由 `bgformat`、`charaformat` 等键决定；音频只认 ogg / mp3 / wav（`amr`、`aac` 不认，`midi` 看后端的 SDL_mixer 构建）；影片要后端链了 FFmpeg 才能播。74 条命令的完整参数见 [CPyMO 指令参考.md](CPyMO%20指令参考.md)。

### 9.6 其他引擎：素材提取与格式统一

四步，顺序不要跳：

1. **先解包拿到散文件。** 各引擎都有自己的封包（KiriKiri 的 `.xp3`、Ren'Py 的 `.rpa`、NScripter 的 `.nsa`/`.sar`、RPG Maker 的加密包、Unity 的 AssetBundle、自研格式）。用各社区现成的解包工具，不要自己写解析器——这件事投入产出比最差。这一阶段只求「能拿到原始图片、音频、脚本」。
2. **摊平成 CPyMO 的目录。** 引擎喜欢用子目录、差分后缀、数字 id 组织资源，CPyMO 只按名字在这几处找：`bg/<名字>.<bgformat>`、`chara/<名字>.<charaformat>`、`se/`、`voice/`、`system/`（系统图清单见 7.2）。资源名是扁平字符串，写不进目录，所以改名是必经一步；给自己留一张「原文件 → 新路径」的映射表。
3. **统一尺寸与格式。** `imagesize` 定一次参考分辨率，所有图跟它对上。老游戏是 640×480 或 4:3 就别改图，改 `imagesize`——坐标是百分比（3.1），立绘位置不会因此错。批量转码用 `cpymo-tool convert`（8.2），剩下的边角用 ffmpeg 或 ImageMagick 补。
4. **统一音频。** 只留 ogg / mp3 / wav，其余（amr、aac、部分 midi）先转掉；音量交给 `bgmvolume`、`vovolume`（2.2）。

一条非技术的红线：**只移植你有权处理的作品和素材**——自研的、明确授权的同人、或你自己买断的东西。素材一旦摊进 `bg/`、`voice/`，就不存在任何权限控制了，CPyMO 也不打算加。

### 9.7 其他引擎：把脚本重写成命令流

这是整个移植里唯一无法自动化的大块，按四步走。

**第一步，只翻一层骨架。** 把原文按场景切段，每段写成 `script/*.txt` 里的一个文件，用 `#goto`/`#call` 串起来。这一步不碰立绘和特效，目标是「文字能一路读到第一个选择支」。CPyMO 不排版文本——对话框由引擎画、按 `textspeed` 逐字出现，所以原文的换行、缩进、字体标签（粗体、注音、内联图）都会丢掉；真要保留，只能自己在 `#lua` 里用 `cpymo.ui` 改写条目文本（Level 1 才有），别指望自动转换带过来。

**第二步，逐个概念对表。**

| 原引擎的概念 | CPyMO 的写法 | 注意 |
| --- | --- | --- |
| 一行对话 / 旁白 | `#say 角色,文本`、`#text 文本` | 名字框与排版由引擎决定，脚本管不了 |
| 换背景 | `#bg 名字` | 名字不带目录和扩展名 |
| 显示立绘 | `#chara` 族（例见 4.3） | 槽位是数字；单条 `#chara` 的摆位规则见 3.4 |
| 选择支 | `select_text` / `select_var` / `select_img` 族 | 结果写进 `FSEL`，从 0 起；PyMO 文本脚本必须一行写完，不能像 ykm 那样拆 `_do` |
| 跳转 / 子程序 | `#goto`、`#call` + `#ret` | 目标是 `script/` 里的文件名，不带扩展名 |
| 条件分支 | `#if 变量,算子,值` | 只有整数比较，没有表达式；算子见 4.6 |
| 变量 | `#set 变量名,值` | 只有字符串，「数字」是字符串里的数字 |
| 音乐 / 音效 / 语音 | `#bgm`、`#se`、`#vo`，停用 `#bgm_stop`、`#se_stop` | 语音按名字进 `voice/` |
| 淡入淡出、抖动、闪白 | `#fade_in`、`#fade_out`、`#quake`、`#flash` | 参数是百分比与时长（3.3） |
| 章节切换 | `#change 脚本名` | 换脚本，并清掉当前状态 |

**第三步，接受三堵墙。**

* **没有函数与循环**：原文里靠 `for` 生成十条对话，就复制成十条命令；靠变量循环的地方改成 `#if` + `#goto` 回跳。
* **没有数据结构**：数组、字典展平成 `v0`、`v1`……这样一批变量名，取值靠 `#if` 链。
* **没有浮点**：所有比较都是整数，`0.5` 这类值自己乘 100 存成整数再比。

**第四步，划清自动化的边界。** 用正则或脚本做「一行对一行」的机械替换（对话行、背景行、立绘行）是有效的；选择支、跳转标签、变量语义这三处必须人工过一遍，因为它们的对应关系不在字面上。指望写个全自动转译器把别的引擎翻成 CPyMO，多半得到一个能跑但行为不对的东西。

### 9.8 两条路共通的推进方式与验收

* **先做一条纵线**：开场 → 第一个选择支 → 其中一条支线 → 结束。跑通再铺量。格式、命名、编码三类错误都会在第一条线上暴露，越晚发现越贵。
* **每加一条线就跑一次**。除了 4.5 的清单，重点盯三个数：对话条数、选择支分支数、脚本文件数。对不上就是漏翻。
* **维护映射表**：原文件 → 新路径、原脚本行 → 新脚本行，两个 CSV 就够。写到后期没有它寸步难行。
* **收尾瘦身**：`cpymo-tool strip` 删掉脚本没引用到的素材，`pack` 把 bg / chara / se / voice 打包（1.1、8.1、8.3）。
* **存档不通用**：只在你自己的移植版本内部兼容，原引擎的存档读不了。CPyMO 的 `save/config.csav` 还会覆盖一部分 `gameconfig`（2.4），测试前先删掉它。

### 9.9 转到 YukimiScript（可选）

两条路都适用。如果打算长期维护这份移植，可以把文本脚本转成 YukimiScript：仓库根有 `pymo2ykm.ps1`（PowerShell，UTF-16LE 编码），用法 `pymo2ykm.ps1 <pymoscript.txt> <输出.ykm>`，逐行读、逐条翻，产出 UTF-8 文本。

**它替你做的事**（以下每条都在脚本里能查到）：

* 先扫一遍全脚本，把每个出现过的角色名定义成 `c0`、`c1`……，写在文件开头（`@__define_character c0 "角色"`）。
* `#say 角色,正文` → 正文行 `c0:正文`；正文含换行的拆成多条。单参数的 `#say 正文` 就是纯文本行。
* `#label 名字` → `@goto "名字"` + 空行 + `- scene "名字"` 三行；`#goto 名字` → `@goto "名字"`。
* 文件开头自动补 `- scene "<脚本名>"`。若第一个 `label` 与脚本同名就跳过这一条，免得重名；否则主场景名改成 `OLD_<脚本名>`，并把同名 `label` / `goto` / `if` 里的引用一起改掉。
* 颜色参数里的 `#` 换成 `0x`（`#FF0000` → `0xFF0000`）；布尔参数 `0` → `false`、其余 → `true`。
* `#lua` **会被翻译**：整行原样取走（不按 `;` 切注释、不按 `,` 切参数），输出 `@lua "…"`，引号与反斜杠转义。
* 文本做全角替换：`[`→`「`、`]`→`」`、`<`→`〈`、`>`→`〉`、`--`→`——`、`"`→`“`；富文本标记 `[文字](style=color~#RRGGBB)` 与 `[汉字](ruby=…)` 原样保留。

**它不替你做的事**：

* 不认识的命令会打印 `Warning: In line N, xxx is not supported.`，并写成 `#命令 参数` 注释行。**语义丢了、行还在**，所以转换完必须全文搜一遍 `^#`。
* 不校验参数个数与类型：缺参会翻成空值，写错的引用会原样翻过去，到 `ykmc` 编译时才报。
* 不搬资源、不改名：`#bg` 里的名字是什么，`@bg` 里还是什么。

转换后再用 `ykmc` 编译回 `script/*.txt`，游戏就能跑。这条路的好处是能用到 Feature Level 1 的 Lua 扩展和新命令；坏处是多一层工具链。小规模移植建议先保持文本脚本，跑通了再说。

---

## 10. 排错速查

**先分清楚是哪一层出的问题**：资源层（找不到文件）、脚本层（命令写错/跳错）、引擎层（崩溃/断言）、后端层（没声音/没画面/乱码）。四层的排查手段完全不同。

| 现象 | 先查 |
| --- | --- |
| 启动就退出 | `gameconfig.txt` 是否存在、`startscript` 指向的 `script/<名>.txt` 是否存在 |
| 卡在第一个画面 | 脚本第一行是不是没有可交互命令（例如只有 `#bg` 而没有 `#say`/`#waitkey`） |
| 报参数无效、某条命令没生效 | 必填参数是空的（引擎 `ENSURE` 判空后返回 `CPYMO_ERR_INVALID_ARG`）。PyMO 文本里一条命令一行写完，**没有「再补一行收尾」这种写法**；收尾行 `_do` 只属于 YukimiScript |
| 文本框位置怪异 | `msgtb` / `msglr` 与 `imagesize` 不匹配，或键名写成了字段名（见 2.2） |
| 窗口是黑的但有声音 | 图形后端初始化失败，看日志里的 `[Error]` |
| 一切正常但字很小 | `fontsize` 被 `save/config.csav` 覆盖，或逻辑分辨率定得太大 |
| 某条 `#if` 没生效 | 条件右侧写了没定义过的变量——引擎取不到就直接跳过整行，不报错（见 4.6） |
| 报 `max value must bigger than min value for rand command.` | `#rand` 的上下界相等了，把上界放大（见 4.6） |
| 立绘位置整个跑偏 | 坐标模式写成了 `cm5`，PyMO 文本里必须是纯数字 `0`–`6`（见 3.4） |
| 文字颜色变成品红 | 写了颜色名，引擎只认 `#RRGGBB`（见 3.2） |

引擎的日志直接打到标准输出，桌面平台上重定向就能存下来：

```bash
./cpymo /path/to/MyGame > game.log 2>&1
```

未知命令会打 `[Warning]`，资源加载失败会打 `[Error]`，两者都值得逐条看。

---

## 11. 接下来读什么

* 具体命令怎么用：[CPyMO 指令参考.md](CPyMO%20指令参考.md)
* YukimiScript 文法：**编译器 `ykmc` 不在本仓库**，它在另一个项目 `Strrationalism/YukimiScript` 里；本仓库只提供 `libpymo.ykm`（CPyMO 侧的命令声明，`- extern` 签名就在这个文件里，写 `.ykm` 的命令名和参数以它为准）。把老 PyMO 脚本转成 YukimiScript 用仓库根的 `pymo2ykm.ps1`，转完再用 `ykmc` 编译回 `script/*.txt`。服务器上另有一份 `YukimiScript_Wiki.md`（未随仓库分发）
* 各平台的构建与部署要求：[README.md](README.md)
* 想给游戏加无障碍（朗读、键盘导航）：README 的「视障帮助」章节，以及 `CPyMO HD.md` 里 Feature Level 的规划
