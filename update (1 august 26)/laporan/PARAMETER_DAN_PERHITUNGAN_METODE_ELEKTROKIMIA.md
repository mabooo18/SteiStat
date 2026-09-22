# Panduan Parameter, Perhitungan, dan Teori Elektrokimia (CA, SWV, DPV)

Dokumen ini menjelaskan secara sistematis tentang **parameter yang digunakan**, **rumus perhitungan matematika dalam kode C++**, dan **kesesuaian dengan teori elektrokimia** untuk tiga metode utama: **Chronoamperometry (CA)**, **Square Wave Voltammetry (SWV)**, dan **Differential Pulse Voltammetry (DPV)** pada firmware SteiStat (Analog Devices AD5941).

---

## 1. Konversi Dasar Hardware: ADC ke Arus ($I$)

Semua metode elektrokimia pada SteiStat membaca arus sel ($I_{\text{cell}}$) melalui penguat transimpedansi (**HSTIA**) dan **16-bit Sigma-Delta ADC**. 

### Rumus Inti Konversi Kode C++ (`RawToCurrent`)

$$I_{\text{cell}} (\text{Ampere}) = \frac{\text{code} \times V_{\text{ref}}}{32768 \times 1000 \times G_{\text{PGA}} \times R_{\text{TIA}}}$$

### Keterangan Komponen Perhitungan:

| Variabel | Nama / Istilah Kode | Nilai / Sumber | Fungsi & Deskripsi |
|---|---|---|---|
| $\text{code}$ | `code` | `(int16_t)(rawCode & 0xFFFF)` | Nilai biner 16-bit *signed two's complement* dari filter Sinc2 ADC. |
| $V_{\text{ref}}$ | `Vref_mV` | `1820.0` mV ($1.82\text{ V}$) | Tegangan acuan internal ADC chip AD5941. |
| $G_{\text{PGA}}$ | `PGA_G` | `1.0` ($1\times$) | Penguatan Programmable Gain Amplifier ADC. |
| $R_{\text{TIA}}$ | `Rf_Ohm` | `rf_values[tia_rf]` | Resistansi umpan balik HSTIA ($\Omega$). Pilihan resistor: $\{200, 1000, 5000, 10000, 20000, 40000, 80000, 160000\}\ \Omega$. |

---

## 2. Chronoamperometry (CA)

### A. Pengertian & Tujuan
Chronoamperometry adalah metode di mana potensial listrik tunggal yang konstan diterapkan pada sel elektrokimia, dan arus listrik yang dihasilkan direkam sebagai fungsi dari waktu.

### B. Parameter Kode (`c_ca.cpp`)

| Parameter | Simbol Teori | Satuan | Deskripsi dalam Kode |
|---|---|---|---|
| `CA_Voltage_mV` | $E$ | mV | Tegangan potensial DC konstan yang diterapkan antara WE dan RE. |
| `CA_Duration_s` | $T_{\text{total}}$ | Detik | Durasi total waktu pengujian. |
| `CA_SampleRate_Hz` | $f_s$ | Hz | Jumlah sampel arus yang diambil per detik. |

### C. Rumus & Perhitungan Matematika

1. **Jumlah Sampel Total ($N_{\text{samples}}$)**:
   $$N_{\text{samples}} = \text{CA Duration (s)} \times \text{CA SampleRate (Hz)}$$

2. **Waktu Pengamatan Titik ke-$i$ ($t_i$)**:
   $$t_i = \frac{i}{f_s} \quad \text{detik} \quad (i = 0, 1, 2, \dots, N_{\text{samples}}-1)$$

3. **Arus Terukur ($I(t_i)$)**:
   $$I(t_i) = \text{RawToCurrent}(\text{ADC Code}_i)$$

### D. Pencocokan dengan Teori Elektrokimia (Persamaan Cottrell)

* **Persamaan Cottrell**:
  $$I(t) = \frac{n F A D^{1/2} C^*}{\pi^{1/2} t^{1/2}}$$
  * $n$: Jumlah elektron yang ditransfer.
  * $F$: Konstanta Faraday ($96,485\text{ C/mol}$).
  * $A$: Luas permukaan elektroda kerja ($cm^2$).
  * $D$: Koefisien difusi spesies aktif ($cm^2/s$).
  * $C^*$: Konsentrasi awal analit ($mol/cm^3$).

* **Pencocokan Kode vs Teori**:
  * **Respon Grafis**: Pada awal penerapan potensial, terjadi lonjakan arus akibat pengisian *capacitive double-layer*. Arus kemudian meluruh secara kuadratik terhadap waktu ($t^{-1/2}$) hingga mencapai arus difusi *steady-state*.
  * **Handling Kode**: Dalam fungsi `MeasureCurrentRaw()`, kode memberikan jeda `delayMicroseconds(500)` sebelum konversi ADC untuk membiarkan arus pengisian kapasitif awal meluruh, sehingga yang terukur murni merupakan **arus faradaik difusi**.

---

## 3. Square Wave Voltammetry (SWV)

### A. Pengertian & Tujuan
SWV adalah teknik voltametri pulsa cepat di mana tegangan berbentuk tangga (*staircase*) ditimpa oleh gelombang kotak simetris. Teknik ini dirancang untuk memaksimalkan arus faradaik dan mengeliminasi arus latar kapasitif.

### B. Parameter Kode (`c_swv.cpp`)

| Parameter | Simbol Teori | Satuan | Deskripsi dalam Kode |
|---|---|---|---|
| `SWV_Start_mV` | $E_{\text{start}}$ | mV | Potensial awal pemindaian. |
| `SWV_End_mV` | $E_{\text{end}}$ | mV | Potensial akhir pemindaian. |
| `SWV_Step_mV` | $\Delta E_s$ | mV | Kenaikan potensial tangga (*staircase step*). |
| `SWV_Amplitude_mV` | $E_{\text{sw}}$ | mV | Amplitudo pulsa gelombang kotak. |
| `SWV_Frequency_Hz` | $f_{\text{sw}}$ | Hz | Frekuensi pulsa gelombang kotak. |
| `SWV_SampleDelay_s` | $t_{\text{wait}}$ | Detik | Waktu stabilisasi sebelum pembacaan ADC. |

### C. Rumus & Perhitungan Matematika

1. **Jumlah Langkah Tangga ($N_{\text{steps}}$)**:
   $$N_{\text{steps}} = \left\lfloor \frac{|E_{\text{end}} - E_{\text{start}}|}{\Delta E_s} \right\rfloor + 1$$

2. **Setengah Periode Pulsa ($t_{\text{half}}$)**:
   $$t_{\text{half}} = \frac{500000}{f_{\text{sw}}} \quad (\mu\text{s})$$

3. **Potensial Pulsa Maju (*Forward*) & Mundur (*Reverse*)**:
   $$V_{\text{forward}} = V_{\text{step}} + E_{\text{sw}}$$
   $$V_{\text{reverse}} = V_{\text{step}} - E_{\text{sw}}$$

4. **Arus Diferensial Bersih ($\Delta I$)**:
   $$\Delta I = I_{\text{forward}} - I_{\text{reverse}}$$

### D. Pencocokan dengan Teori Elektrokimia

* **Korelasi Kode vs Teori**:
  * **Eliminasi Arus Kapasitif**: Pada pulsa maju ($V_{\text{forward}}$), reaksi reduksi diukur ($I_{\text{forward}}$). Pada pulsa mundur ($V_{\text{reverse}}$), reaksi re-oksidasi diukur ($I_{\text{reverse}}$). Karena arus pengisian kapasitif bernilai hampir persis sama pada kedua pulsa, penguraian diferensial $\Delta I = I_f - I_r$ menghapuskan arus kapasitif latar secara total.
  * **Bentuk Puncak**: Kurva plot SWV ($\Delta I$ vs $V_{\text{step}}$) menghasilkan puncak (*peak*) yang sangat tajam pada potensial standar formal (${E^0}'$), dengan sensitivitas hingga orde nanomolar ($nM$).

---

## 4. Differential Pulse Voltammetry (DPV)

### A. Pengertian & Tujuan
DPV menerapkan pulsa tegangan periodik di atas pindaian tangga linier. Arus diukur dua kali pada setiap siklus langkah (sebelum pulsa dan di akhir pulsa) untuk memisahkan respon faradaik dari arus latar.

### B. Parameter Kode (`c_dpv.cpp`)

| Parameter | Simbol Teori | Satuan | Deskripsi dalam Kode |
|---|---|---|---|
| `DPV_Start_mV` | $E_{\text{start}}$ | mV | Potensial awal pemindaian. |
| `DPV_End_mV` | $E_{\text{end}}$ | mV | Potensial akhir pemindaian. |
| `DPV_Step_mV` | $\Delta E_s$ | mV | Kenaikan potensial tangga. |
| `DPV_Amplitude_mV` | $E_{\text{pulse}}$ | mV | Amplitudo pulsa tegangan. |
| `DPV_PulseWidth_s` | $t_w$ | Detik | Durasi/lebar pulsa tegangan. |
| `DPV_PulsePeriod_s` | $T_p$ | Detik | Total waktu satu siklus pulsa. |
| `DPV_SampleDelay_s` | $t_{\text{wait}}$ | Detik | Waktu stabilisasi sebelum sampling. |

### C. Rumus & Perhitungan Matematika

1. **Jumlah Langkah Tangga ($N_{\text{steps}}$)**:
   $$N_{\text{steps}} = \left\lfloor \frac{|E_{\text{end}} - E_{\text{start}}|}{\Delta E_s} \right\rfloor + 1$$

2. **Pembacaan Arus Basis ($I_{\text{base}}$)**:
   Diterapkan pada potensial tangga $V_{\text{step}}$, tepat sebelum pulsa diberikan.

3. **Pembacaan Arus Pulsa ($I_{\text{pulse}}$)**:
   Diterapkan pada potensial puncak $V_{\text{pulse}} = V_{\text{step}} + E_{\text{pulse}}$.

4. **Arus Diferensial Bersih ($\Delta I$)**:
   $$\Delta I = I_{\text{pulse}} - I_{\text{base}}$$

### D. Pencocokan dengan Teori Elektrokimia

* **Korelasi Kode vs Teori**:
  * **Peluruhan Waktu (Time Decay)**: Arus kapasitif meluruh secara eksponensial jauh lebih cepat daripada arus faradaik. Dengan mengukur arus di akhir pulsa ($I_{\text{pulse}}$) dan menguranginya dengan arus basis ($I_{\text{base}}$), kontribusi arus pengisian latar tereliminasi.
  * **Bentuk Puncak Lonceng (*Bell-Shaped Peak*)**: Grafik DPV ($\Delta I$ vs $V_{\text{step}}$) menghasilkan puncak simetris di mana tinggi puncak ($\Delta I_p$) berbanding lurus secara linier dengan konsentrasi spesies kimia dalam larutan.

---

## 5. Tabel Perbandingan Ringkas Antar Metode

| Aspek / Karakteristik | Chronoamperometry (CA) | Square Wave Voltammetry (SWV) | Differential Pulse Voltammetry (DPV) |
|---|---|---|---|
| **Domain Sumbu-X** | Waktu ($t$ dalam detik) | Potensial Tangga ($V_{\text{step}}$ dalam mV) | Potensial Tangga ($V_{\text{step}}$ dalam mV) |
| **Domain Sumbu-Y** | Arus Murni ($I$ dalam Ampere) | Arus Diferensial ($\Delta I = I_f - I_r$) | Arus Diferensial ($\Delta I = I_{\text{pulse}} - I_{\text{base}}$) |
| **Profil Tegangan** | DC Konstan | Tangga + Gelombang Kotak Simetris | Tangga + Pulsa Periodik Searah |
| **Respon Kurva** | Peluruhan Eksponensial ($t^{-1/2}$) | Puncak Tajam (*Sharp Peak*) | Puncak Lonceng (*Bell-shaped Peak*) |
| **Fungsi Utama** | Studi kinetika difusi & waktu reaksi | Analisis kuantitatif cepat & sensitivitas tinggi | Analisis jejak analit (*trace analysis*) |
