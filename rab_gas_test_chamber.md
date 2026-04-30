# Rancangan Anggaran Biaya Gas Test Chamber

Tanggal riset harga: 2026-04-30  
Prioritas marketplace: Shopee / Tokopedia. Jika hasil Tokopedia tidak stabil atau tidak muncul di indeks pencarian, digunakan Shopee, Blibli, atau supplier instrumen Indonesia sebagai pembanding.

## Catatan Penting

- Harga marketplace cepat berubah. Angka di bawah adalah estimasi budgeting, belum termasuk ongkir, pajak, diskon, atau stok habis.
- RAB ini untuk chamber pengujian awal, terutama LPG/butane/propane-family.
- CO dan H2S tidak direkomendasikan untuk chamber DIY tanpa instrumen tersertifikasi, ventilasi, prosedur darurat, dan supervisi lab.
- `reference_ppm` untuk model ML harus berasal dari instrumen referensi terkalibrasi, bukan dari MQ sensor atau estimasi volume gas manual.
- Figaro TGS2610 tetap berguna sebagai cross-check LPG-family, tetapi bukan universal ppm reference.

## Rekomendasi Paket

### Paket A - Starter LPG-Family Chamber

Tujuan:

- validasi mekanik chamber
- logging clean air, butane/LPG/propane-family, recovery
- belum untuk klaim ppm produksi
- belum untuk CO/H2S

| No  |                                                           Item |   Qty | Estimasi Harga Satuan |           Total | Catatan                                                 |
| --- | -------------------------------------------------------------: | ----: | --------------------: | --------------: | ------------------------------------------------------- |
| 1   |                                    Box akrilik 30 x 30 x 30 cm |     1 |             Rp243.000 |       Rp243.000 | Basis chamber kecil, perlu modifikasi port dan gasket   |
| 2   |          Modifikasi port, gasket silikon, baut, dudukan sensor | 1 lot |             Rp150.000 |       Rp150.000 | Estimasi lokal/bengkel                                  |
| 3   |                                                  BME280 module |     1 |              Rp58.500 |        Rp58.500 | Temperatur, kelembapan, tekanan                         |
| 4   |                                        Kipas brushless DC 12 V |     1 |              Rp35.000 |        Rp35.000 | Mixing fan internal rendah tegangan                     |
| 5   |                                                Cable gland PG7 |     5 |               Rp2.500 |        Rp12.500 | Untuk kabel sensor/fan                                  |
| 6   |                                       Selang PU pneumatic 6 mm |   5 m |               Rp6.100 |        Rp30.500 | Tubing gas/udara ringan                                 |
| 7   |                                         Fitting pneumatic 6 mm |    10 |               Rp5.000 |        Rp50.000 | Straight, elbow, tee, bulkhead                          |
| 8   |                                         Solenoid valve 12 V NC |     2 |             Rp157.500 |       Rp315.000 | Gas inlet dan clean-air inlet; letakkan di luar chamber |
| 9   |                                              Regulator LPG SNI |     1 |              Rp48.138 |        Rp48.138 | Untuk fase LPG-family sederhana                         |
| 10  |                                  Rotameter gas/udara sederhana |     1 |             Rp190.000 |       Rp190.000 | Minimal flow indication, bukan mass flow controller     |
| 11  | Portable combustible gas leak detector / LEL detector low-cost |     1 |             Rp750.000 |       Rp750.000 | Safety monitor eksternal, bukan reference-grade         |
| 12  |                                           Power supply 12 V DC |     1 |             Rp100.000 |       Rp100.000 | Fan + solenoid                                          |
| 13  |           Clamp, seal tape, bracket, kabel, konektor, terminal | 1 lot |             Rp150.000 |       Rp150.000 | Consumables                                             |
|     |                                                   **Subtotal** |       |                       | **Rp2.132.638** |                                                         |
|     |                                               **Cadangan 15%** |       |                       |   **Rp319.896** |                                                         |
|     |                                     **Estimasi Total Paket A** |       |                       | **Rp2.452.534** | Bulatkan: **Rp2,5 juta**                                |

Rekomendasi penggunaan:

- cocok untuk mulai menguji alur chamber, logging, baseline, injection, recovery
- gunakan hanya untuk LPG-family pada konsentrasi rendah dan ventilasi baik
- jangan pakai untuk CO/H2S
- jangan klaim ppm akurat dari paket ini

## Paket B - Improved Controlled-Flow LPG/Methane Chamber

Tujuan:

- chamber lebih repeatable
- kontrol aliran lebih baik
- safety/reference lebih kuat untuk combustible gas
- lebih cocok untuk dataset ML awal yang rapi

Mulai dari Paket A, lalu upgrade:

| No | Upgrade Item | Qty | Estimasi Harga Satuan | Tambahan Biaya | Catatan |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | Chamber akrilik custom 10-30 L dengan port rapi | 1 | Rp750.000 | +Rp507.000 | Upgrade dari box akrilik sederhana |
| 2 | Gas rotameter LZM-6T 15-20 L/min | 1 | Rp1.049.000 | +Rp859.000 | Lebih sesuai untuk gas dibanding rotameter murah |
| 3 | 4-in-1 gas detector CO/H2S/O2/LEL | 1 | Rp2.250.000 | +Rp1.500.000 | Replace detector murah; tetap perlu kalibrasi |
| 4 | BME280/SHT module kualitas lebih baik | 1 | Rp148.400 | +Rp89.900 | Upgrade dari modul murah |
| 5 | Exhaust fan/ducting kecil + bracket | 1 lot | Rp300.000 | +Rp300.000 | Venting ke lokasi aman |
| 6 | Extra fittings, check valve, quick connector | 1 lot | Rp250.000 | +Rp250.000 | Membuat flow path lebih mudah dirawat |
|  | **Tambahan dari Paket A** |  |  | **Rp3.505.900** |  |
|  | **Estimasi Total Paket B** |  |  | **Rp5.958.434** | Bulatkan: **Rp6,0 juta** |

Rekomendasi penggunaan:

- paket yang paling masuk akal untuk dataset LPG-family dan methane awal
- tetap gunakan `reference_lel_percent`, bukan langsung `reference_ppm`, jika instrumen hanya memberi `%LEL`
- untuk methane, gunakan detektor methane/LEL yang jelas spesifikasinya

## Paket C - Toxic Gas / Reference-Grade Upgrade

Tujuan:

- CO dan H2S hanya untuk lingkungan yang punya keselamatan lab
- fokus pada instrumen referensi dan alarm keselamatan
- bukan rekomendasi untuk DIY terbuka

| No | Item | Qty | Estimasi Harga Satuan | Total | Catatan |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 | 4-in-1 detector CO/H2S/O2/LEL low-mid range | 1 | Rp2.199.000 - Rp3.600.000 | Rp2.199.000 - Rp3.600.000 | Minimum safety monitor, perlu cek kalibrasi |
| 2 | 4-gas detector brand industrial higher-end | 1 | Rp13.320.000 | Rp13.320.000 | Contoh BW Max XT II, lebih dekat ke industrial |
| 3 | CO meter electrochemical portable | 1 | Rp495.000+ | Rp495.000+ | Untuk CO only; cek sertifikat/kalibrasi |
| 4 | Certified calibration gas / jasa kalibrasi | 1 lot | RFQ | RFQ | Wajib untuk reference-grade |
| 5 | Exhaust/fume handling dan safety procedure | 1 lot | RFQ | RFQ | Wajib untuk CO/H2S |

Rekomendasi:

- Untuk CO/H2S, lebih baik gunakan alat multi-gas terkalibrasi daripada raw sensor murah.
- Jika ingin raw sensor untuk sistem elektronik sendiri, gunakan Alphasense CO/H2S atau sensor sekelasnya, tetapi tetap perlu analog front-end/potentiostat, kalibrasi, dan kompensasi lingkungan.
- CO dan H2S dataset tidak boleh dimulai sebelum instrumen referensi, alarm eksternal, ventilasi, dan SOP keselamatan tersedia.

## Item Yang Direkomendasikan Untuk Dibeli Lebih Dulu

Urutan pembelian paling pragmatis:

1. Box/chamber akrilik kecil atau custom chamber 10-30 L.
2. BME280/SHT sensor.
3. Fan brushless 12 V.
4. Cable gland, gasket, fittings, tubing.
5. Dua solenoid 12 V normally-closed.
6. Regulator dan rotameter.
7. Portable combustible gas detector / LEL detector.
8. Setelah chamber stabil: upgrade ke 4-in-1 gas detector atau reference instrument.

## Item Yang Jangan Diprioritaskan Dulu

- Raw H2S sensor tanpa calibration setup.
- Raw CO sensor untuk klaim ppm tanpa kalibrasi.
- Mass flow controller mahal sebelum chamber dasar terbukti bekerja.
- CO/H2S gas testing sebelum SOP lab dan alat keselamatan tersedia.

## Sumber Harga Dan Rujukan

| Kebutuhan | Sumber | Harga / Info yang dipakai |
| --- | --- | --- |
| Box akrilik 30 x 30 x 30 cm | Blibli: Kotak Amal Akrilik 30 x 30 x 30 cm | Rp243.000 |
| BME280 murah | Blibli: GY-BME280 module | Rp58.500 |
| BME280 lebih baik | Digiware: BME280 Environmental Sensor | Rp148.400 |
| Kipas brushless 12 V | Blibli listing brushless/DC fan | kisaran Rp10.000 - Rp125.000; dipakai Rp35.000 |
| Cable gland PG7 | Blibli/Lazada listings | sekitar Rp2.000 - Rp5.660 |
| Selang PU 6 mm | BigGo/Shopee aggregate | sekitar Rp5.000 - Rp6.200 per meter |
| Fitting pneumatic 6 mm | Blibli/BibitBunga/Shopee aggregate | sekitar Rp2.500 - Rp9.500 per pcs |
| Solenoid valve 12 V | BigGo/Shopee aggregate | sekitar Rp157.500 untuk solenoid kuningan 12 V |
| Regulator LPG SNI | Shopee | Rp48.138 |
| Rotameter sederhana | Shopee search | sekitar Rp190.000 untuk flowmeter oksigen/gas sederhana |
| Gas rotameter LZM-6T | Shopee | Rp1.049.000 |
| Portable combustible detector | Shopee gas detector listings | sekitar Rp547.000 - Rp1.150.000 |
| 4-in-1 gas detector | SNDWAY Indonesia | Rp2.250.000 |
| 4-in-1 gas detector alternative | Riverve / SatuLab / SR Online | Rp2.199.000 - Rp13.320.000 |
| Flammable reference sensor | NevadaNano MPS official | `%LEL`, methane/propane/butane support |
| CO reference sensor | Alphasense official | electrochemical CO sensors |
| H2S reference sensor | Alphasense official | electrochemical H2S sensors |
| VOC context sensor | Sensirion SGP40 official | VOC index/interference context, not ppm ground truth |

## Link Referensi

- Shopee rotameter search: https://shopee.co.id/search?keyword=rotameter+flow+meter
- Shopee LZM-6T gas rotameter 15 L/min: https://shopee.co.id/Dn12-1-4-Inch-Flow-15-L-Min-Lzm-6T-Rotameter-%28Gas%29-i.164601711.27812346631
- Shopee gas detector search: https://shopee.co.id/search?keyword=gas+detectors
- Shopee LPG regulator example: https://shopee.co.id/Regulator-Gas-LPG-Elpiji-SNI-Termurah-dan-Terbaik-i.641642292.15242832864
- SNDWAY 4-in-1 detector: https://www.sndway.id/product-page/sndway-gas-detector-4-in-1-co-h2s-o2-combustible-gas-sensor-sw-7500a-pro
- Blibli acrylic box 30 x 30 x 30 cm: https://www.blibli.com/p/kotak-amal-akrilik-ukuran-besar-30x30x30-cm/ps--DUC-60031-00363
- Blibli GY-BME280: https://www.blibli.com/p/gy-bme280-bme-280-sensor-temperatur-kelembapan-barometric-tekanan-iic/ps--ITE-70030-00189
- Digiware BME280: https://digiwarestore.com/en/sensor/bme280-environmental-sensor-temperature-humidity-barometric-pressure-296523.html
- Blibli DC fan listings: https://www.blibli.com/jual/dc-blower-fan-12-v
- Blibli cable gland PG7 listings: https://www.blibli.com/jual/gland-kabel-pg7-pvc
- BigGo PU hose/fittings Shopee aggregate: https://biggo.id/s/pneumatic%20selang%206mm
- BigGo solenoid 12 V Shopee aggregate: https://biggo.id/s/Solenoid%2BValve%2B12V
- NevadaNano MPS flammable gas sensor: https://nevadanano.com/products/mps-flammable-gas-sensor/
- Alphasense CO sensors: https://www.alphasense.com/products/view-by-target-gas/carbon-monoxide-sensors-co
- Alphasense H2S sensors: https://www.alphasense.com/products/view-by-target-gas/hydrogen-sulphide-sensors-h2s
- Sensirion SGP40: https://sensirion.com/sgp40
