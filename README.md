# Atherta 3D Paint — Krita için 3D boyama

UV'si açılmış bir modeli (OBJ / FBX) Krita'ya yükleyip **Krita'nın aktif fırçasıyla
doğrudan 3D yüzeyin üzerine** boyamanı sağlayan bir Krita 5.2 eklentisi (C++).

- Fırça Krita'nın kendi fırça motoru: basınç eğrisi, yumuşatma/stabilizer,
  airbrush, tüm preset'ler ve renkler Krita'da nasıl çalışıyorsa öyle çalışır.
- Dokular Krita belgesinin katmanlarıdır. Her PBR kanalı bir grup katmandır;
  içine istediğin kadar katman, maske ve filtre ekleyebilirsin. 2D tuvalde
  yaptığın her şey anında 3D önizlemeye yansır.
- 3D görünüm Blender'ın varsayılan kontrollerini ve hızlarını birebir kullanır.
- PBR önizleme: Base Color, Roughness, Metallic ve Normal kanalları, Substance
  Painter'daki gibi ayrı ayrı görüntülenip boyanabilir.

## Kullanım

1. **Settings › Dockers › 3D Paint** ile paneli aç. Büyük bir görünüm için
   paneli yüzer hale getirebilirsin.
2. **Load Model...** ile UV'si açılmış bir `.obj` veya `.fbx` dosyası yükle.
3. **Texture Set ›**
   - **New Texture Document** (1024/2048/4096): Yeni bir doku belgesi açar.
   - **Create in Current Document**: Açık belgeye kanal katmanlarını ekler.
     Belge boyutu doku çözünürlüğüdür.
4. **Paint:** satırından hangi kanalların boyanacağını seç:
   - **Color:** Fırça rengini Base Color'a uygular.
   - **Rough / Metal:** Kaydırıcıdaki değeri yazar. Fırça vuruşu bu değer
     için maske görevi görür.
   - **Normal:** Fırça rengini Normal kanalına uygular.
5. Sol tık veya kalem ucuyla modelin üzerine boya. Her vuruş tek bir geri alma
   adımıdır (Ctrl+Z).
6. **View:** listesi ya da viewport'ta **C** tuşu ile Material / Base Color /
   Roughness / Metallic / Normal görünümleri arasında geçiş yap.
7. **Texture Set › Export Textures...** her kanalı PNG olarak dışa aktarır
   (`model_basecolor.png`, `model_roughness.png`, ...).

Fırça boyutu ekran pikseli cinsindendir, yani 3D görünümde gördüğün boyuttur.
Fırça ucunu her zaman Krita'nın kendi panellerinden seçersin. Krita'da bir seçim
varsa (UV uzayında) boya yalnızca seçili alana uygulanır.

### Belgedeki katman yapısı

```
Atherta 3D
├── Base Color          (grup: 3D önizleme bu grubun sonucunu okur)
│   ├── Base Color Paint    ← varsayılan boyama katmanı
│   └── Base Color Base     (kanalın varsayılan değeri, bellek harcamaz)
├── Roughness
├── Metallic
└── Normal
```

Bir kanal grubunun içindeki herhangi bir boya katmanını aktif yaparsan 3D
vuruşlar o katmana gider. Değilse grubun en üstteki düzenlenebilir katmanına
gider. Kanal gruplarını görünür bırak: Krita gizli grupların sonucunu
güncellemeyebilir.

## Kontroller

3D görünüm Blender'ın varsayılan tuş haritasını ve hızlarını kullanır. Değerler
Blender kaynak kodundan alınmıştır (`view3d_navigate*.cc`, `DNA_userdef_types.h`):

| Giriş | Eylem |
|---|---|
| Orta tuş sürükle | Döndür (turntable, piksel başına 0.4°) |
| Shift + orta tuş | Kaydır (pan) |
| Ctrl + orta tuş | Yakınlaştır (dolly stili sürükleme) |
| Shift + Ctrl + orta tuş | Dolly (pivotu bakış yönünde ilerlet) |
| Tekerlek | Yakınlaştır / uzaklaştır (her çentikte ×1.2) |
| Döndürürken Alt | En yakın eksen görünümüne yapış |
| Alt + orta tuş tıkla | Tıklanan noktayı merkez yap |
| Alt + orta tuş sürükle | Yöne göre Top / Bottom / Left / Right / Front / Back |
| Numpad 1 / 3 / 7 | Front / Right / Top (Ctrl ile Back / Left / Bottom) |
| Numpad 2 / 4 / 6 / 8 | 15° döndür; Ctrl ile kaydır; Shift+4/6 ile roll |
| Numpad 5 | Perspektif / ortografik |
| Numpad 9 | Karşı taraf |
| Numpad + / −, Ctrl + = / − | Yakınlaştır / uzaklaştır |
| Home, Numpad ., Shift+C, fare 4. tuş | Modeli çerçevele |
| C | Kanal görünümleri arasında geç |
| Sol tık / kalem ucu | Boya |
| Kalemin silgi ucu | Krita'nın silgi preset'ine geçer ve siler |
| Space + sürükle | Döndür (Shift: kaydır, Ctrl: yakınlaştır). Orta tuşu olmayan kalemler için |
| Kalem yan tuşu (orta tuş) | Blender'daki gibi gezinme |
| Tek parmak | Döndür |
| İki parmak | Kaydır + pinch ile yakınlaştır |

- **Avuç reddi:** Kalem ekrana yakınken veya kalem olayından sonraki 0.5 sn
  içinde gelen dokunuşlar yok sayılır.
- **Parmakla boyama:** Varsayılan olarak kapalıdır, ayarlardan açılabilir.

Ayarlar menüsünde (⚙) şu Blender tercihleri de var: Trackball orbit, Zoom to
Mouse Position, Auto Perspective, Invert Zoom ve Emulate 3 Button Mouse
(Alt + sol tık). Ek olarak ızgara ve DirectX normal map (Y−) seçenekleri bulunur.

## Nasıl çalışıyor

1. Vuruş, Krita'nın `KisToolFreehandHelper` sınıfıyla, görünüm boyutunda gizli
   bir `KisImage` üzerine aktif fırçayla çizilir. Canlı önizleme bu ekran
   katmanını modelin üzerine yansıtır.
2. Vuruş bittiğinde GPU, modeli UV uzayında rasterize eder. Her texel'i vuruş
   kamerasına izdüşürür ve derinlik testiyle yalnızca görünen yüzeyi alır.
   Kameraya çok eğik yüzeylerde boya yumuşakça azalır.
3. Boya, UV adalarının dışındaki boşluğa birkaç piksel taşırılır. Böylece
   dikişlerde arka plan görünmez. Sonuç `KisProcessingApplicator` ile tek
   bir geri alınabilir adım olarak kanal katmanlarına yazılır.
4. Katmanlarda değişiklik oldukça (`sigImageUpdated`) yalnızca değişen
   bölge GPU'ya yüklenir.

## Derleme

Krita eklentileri Krita'nın iç kütüphanelerine (kritaui, kritaimage, ...)
bağlanır. Resmi Krita kurulumu bu kütüphanelerin başlık dosyalarını ve
geliştirici dosyalarını içermediği için eklenti, **Krita 5.2'nin kaynak
koduyla birlikte** derlenir. Krita'nın kaynaklarında hiçbir dosya
değiştirilmez. Tek gereken, Krita'yı yapılandırırken bir CMake parametresi
eklemek:

```
-DCMAKE_PROJECT_krita_INCLUDE=<Atherta yolu>/cmake/KritaInject.cmake
```

Bu dosya, Krita'nın CMake projesinin sonunda eklentiyi
(`kritaatherta3d`) normal bir Krita eklentisi gibi derleme ağacına ekler.
CMake 3.19 veya üstü gerekir.

### Windows

1. Krita'nın resmi rehberini izleyip **`krita/5.2` dalını** derle:
   https://docs.krita.org/en/untranslatable_pages/building_krita.html
2. Krita'nın CMake yapılandırma komutuna yukarıdaki parametreyi ekle:
   `-DCMAKE_PROJECT_krita_INCLUDE=C:/yol/Atherta/cmake/KritaInject.cmake`
3. Derle ve kur (`ninja install`). Eklenti `lib/kritaplugins/kritaatherta3d.dll`
   olarak kurulur ve o Krita derlemesinde çalışır.

Eklentiyi resmi Krita 5.2 kurulumuna kopyalamak, ancak tamamen aynı sürüm,
derleyici ve bağımlılıklarla derlendiğinde çalışabilir. Bu denenmedi; kendi
derlediğin Krita'yı kullanman önerilir.

### Linux (bu repo bu şekilde test edildi)

```bash
git clone --depth 1 --branch krita/5.2 https://invent.kde.org/graphics/krita.git
# Bağımlılıklar: Krita'nın rehberine bak (Ubuntu 24.04'te apt paketleri yeterli).
cmake -S krita -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=$HOME/krita-install \
      -DCMAKE_PROJECT_krita_INCLUDE=$PWD/Atherta/cmake/KritaInject.cmake
cmake --build build && cmake --install build
```

## Testler

- **Çekirdek** (Qt ve Krita gerektirmez): Blender navigasyonu, OBJ/FBX yükleme,
  BVH, UV boşluğu dolgusu ve bake bölgesi.
  ```bash
  cmake -S . -B build-core && cmake --build build-core && build-core/tests/atherta_core_tests
  ```
  `ATHERTA_TEST_DATA=<ufbx/data>` ile gerçek Maya ve Blender FBX dosyaları da
  test edilir.
- **GL entegrasyon testi:** Krita yapılandırmasına `-DATHERTA_BUILD_GL_TEST=ON`
  ekle, sonra `atherta_gl_test [--gles] <çıktı-klasörü>` komutunu çalıştır.
  Bu test:
  - viewport'u gerçek bir GL bağlamında açar
  - tüm görünüm modlarını çizer
  - vuruş bake eder
  - görünür texel'lerin boyandığını ve arka yüzlerin boyanmadığını doğrular
  
  Masaüstü GL 3.3 ve GLES 3.0'da (Windows'taki ANGLE yolu) çalışır.

## Bilinen sınırlamalar

- Kalem basıncı, eğim ve dokunmatik yalnızca kodda var. Bulut ortamında
  gerçek bir tablet ve Windows olmadığı için test edilemedi; Windows'ta
  denenmesi gerekiyor.
- Tek doku seti: modelin tüm malzemeleri aynı doku setini paylaşır. UDIM
  desteklenmiyor (0–1 dışındaki UV'ler sarılır).
- 3D'de Krita'nın 2D ayna (mirror) aracı kapalıdır; 3D simetri henüz yok.
- Smudge / blur gibi alttaki rengi okuyan fırçalar 3D vuruşta boş bir katman
  görür. Normal boyama fırçaları, silgi ve karışım modları çalışır.
- Normal kanalı fırça rengiyle boyanır. Height'tan normal üretimi yok.
- Belge 8-bit sRGB olmalıdır. Lineer veya float belgelerde roughness ve
  metallic değerleri gamma'dan etkilenir.

## Lisans

GPL-3.0-or-later (Krita ile aynı). Navigasyon davranışı Blender'dan
(GPL-2.0-or-later) uyarlanmıştır. Gömülü kütüphaneler:

| Kütüphane | Sürüm | Lisans |
|---|---|---|
| ufbx | 0.23.1 | MIT veya Public Domain |
| MikkTSpace | — | zlib |
| doctest | 2.5.3 | MIT; yalnızca testler için |
