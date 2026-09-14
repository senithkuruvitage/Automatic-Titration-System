# Automated Titration System

An **Automated Titration System (ATS)** is a laboratory automation system designed to perform titration and pH adjustment with minimum human involvement. The system combines pH and temperature sensing, automatic liquid dosing, controlled mixing, data logging, and a user-friendly control interface.

The main goal of this project is to reduce human errors, save time and chemicals, improve laboratory safety, and make titration easier and more reliable.

---

## 📌 Project Overview

Traditional titration requires a person to manually measure and add chemicals, observe the endpoint, control mixing, record measurements, and perform calculations. These steps can introduce errors and require significant time and attention.

The Automated Titration System automates these activities using sensors, stepper motors, a dosing mechanism, a mixing system, and a microcontroller-based control system.

The system can be used for titration experiments involving **acidic and basic solutions** and can automatically adjust the solution toward **pH 7** when required.

---

## 🎯 Main Objectives

* Automate the titration process.
* Reduce human errors during titration.
* Accurately control the amount of acid or base added.
* Measure pH and temperature during the experiment.
* Improve laboratory safety.
* Reduce unnecessary chemical usage.
* Save time and cost.
* Provide an easy-to-use user interface.
* Store previous experimental results for later analysis.
* Generate useful graphs and experimental data.
* Provide both automatic and manual control options.

---

# ⚙️ Main Features

## 1. pH Measurement

The system uses a **pH sensor** to continuously monitor the pH value of the solution.

The pH sensor is designed so that:

* Its height can be adjusted.
* It can be removed when required.
* It can be cleaned easily.
* It can be positioned according to the size and liquid level of the container.

This makes the system easier to maintain and improves measurement reliability.

---

## 2. Temperature Measurement

A **temperature sensor** is included to monitor the temperature of the solution during the titration process.

The temperature sensor can also be adjusted, removed, and cleaned when necessary.

Temperature data can be used together with pH measurements to provide better experimental information.

---

# 💧 Automatic Chemical Dosing System

The system uses:

* **2 × 50 mL syringes**
* **2 × Stepper motors**
* A **3D-printed pump mechanism**

The two syringes can be used to automatically dose the required liquids during the titration process.

The stepper motors provide controlled movement of the syringes, allowing the system to add chemicals in controlled amounts.

### Syringe Air-Error Reduction

A special design is used to reduce measurement errors caused by trapped air inside the syringes.

During refilling, the syringes are positioned **vertically**. This helps reduce the possibility of air remaining inside the syringe.

As a result, the liquid leaving the syringe represents the intended liquid volume more accurately.

This improves dosing accuracy and reduces unnecessary chemical waste.

---

# 🔄 Automatic Mixing System

A controlled mixing mechanism is included to ensure that the added chemical is properly mixed with the solution.

The mixing system consists of:

* Brushed DC motor
* 3D-printed mechanical component
* 2 × Neodymium N48 magnets
* PWM speed control
* White LED lights
* 25 × 8 mm stir bar

The motor speed can be controlled using **PWM (Pulse Width Modulation)**.

This allows the user/system to increase or decrease the stirring speed according to the experimental requirement.

The magnetic coupling allows the motor to rotate the stir bar without directly placing the motor inside the liquid.

---

# 🔬 Automatic Titration Process

The system can perform the titration process automatically.

A simplified process is:

```text
Start
  ↓
Enter Experimental Data
  ↓
Place Solution in Beaker
  ↓
Measure Initial pH & Temperature
  ↓
Start Stirring
  ↓
Add Acid/Base Automatically
  ↓
Mix the Solution
  ↓
Measure pH
  ↓
Check Target pH
  ↓
If Target Not Reached
  ↓
Continue Controlled Dosing
  ↓
Target pH Reached
  ↓
Stop Dosing
  ↓
Generate Experimental Data
  ↓
Save Results
  ↓
End
```

The target pH can be selected according to the experimental requirement. For the main application, the system can be used to adjust a solution toward **pH 7**.

---

# 📊 pH Graph & Concentration Analysis

During a titration experiment, the system can record pH values while chemicals are added.

The collected data can be used to generate a **pH vs. added volume graph**.

The system can also be developed to estimate the concentration of the unknown solution in **mol dm⁻³** using the recorded titration data.

This allows the user to:

* Observe the pH change.
* Analyze the titration curve.
* Identify important changes during titration.
* Estimate the concentration of the unknown solution.
* Determine the approximate amount of acid/base required to reach the target pH.

---

# 📱 User Control

The system is designed to be user-friendly.

The titration process can operate automatically, while a **mobile phone interface** can be used when manual control is required.

The user can enter experimental information and control required system functions through the interface.

The system is designed to make data entry simple and easy to understand.

---

# 📁 Experimental Data & Excel Reports

The system can maintain records of previous titration experiments.

Experimental results can be provided in an **Excel-compatible format**, allowing users to review previous tests.

Stored data can include information such as:

* Date and time
* Initial pH
* Temperature
* Added chemical volume
* pH measurements
* Final pH
* Titration results
* Calculated values
* Experimental status

This allows users to compare previous experiments and perform further analysis.

---

# 💡 Status Indicators

The system includes visual indicators to clearly show its current status.

| Indicator | Meaning                                  |
| --------- | ---------------------------------------- |
| 🔵 Blue   | Power ON                                 |
| 🟡 Yellow | System operating / titration in progress |
| 🟢 Green  | Titration completed                      |
| 🔴 Red    | System error                             |

These indicators make it easier for the user to understand the current system condition.

---

# 🛡️ Safety Features

Safety is an important part of the system design.

The system includes:

### Fuse Protection

A fuse is included to protect the electronic system from excessive current conditions.

### Emergency Power OFF

An emergency power-off switch is provided so that the user can quickly stop the system when necessary.

### Protective Enclosure

The electronic circuits are enclosed to provide protection against accidental contact with:

* Water
* Acid
* Base
* Other liquids

This reduces the risk of liquid reaching the electronic components.

> **Safety note:** Chemical handling should always follow appropriate laboratory safety procedures. The system does not replace laboratory safety equipment or supervision.

---

# 🔌 Power System

The system is designed around a **12 V power system** and can be operated from an external power source.

The architecture can also be extended to support a **rechargeable battery**, allowing the system to operate when the main power supply is unavailable.

The power system includes protection and emergency shutdown features to improve operational safety.

---

# 🌊 Motivation

The project was motivated by environmental and water-management problems associated with changing climate conditions.

During **El Niño** conditions, increased heat and drought can contribute to water shortages and increase the need for alternative water resources.

During **La Niña** conditions, increased rainfall and surface runoff can contribute to water pollution and changes in river-water quality.

Therefore, automated water-quality testing and pH adjustment can be useful in different water-treatment and laboratory applications.

---

# 🌍 Potential Applications

The Automated Titration System can be further developed for use in:

### 🔬 Research & Chemical Laboratories

* Automated laboratory experiments
* Chemical analysis
* pH adjustment
* Repeated titration experiments

### 🚰 Drinking Water Treatment

The system can be developed as a subsystem for water-quality testing and pH adjustment in drinking-water treatment systems.

### 🏭 Industrial Water Treatment

The system can be adapted for monitoring and controlling water used in industrial processes.

### 🐟 Aquaculture Systems

pH monitoring and adjustment are important for maintaining suitable water conditions for aquatic organisms.

### 🌱 Agricultural Water Management

The system can be used for monitoring and adjusting water quality used in agricultural applications.

---

# 🌊 Future Water-Treatment Integration

One possible future development is integrating the Automated Titration System into a larger **seawater-to-drinking-water treatment system**.

In this concept, the Automated Titration System would work as a subsystem responsible for:

* pH measurement
* Water-quality monitoring
* Acid/base dosing
* pH adjustment
* Experimental data recording

The system could also be adapted for testing polluted river water caused by increased rainfall and runoff.

---

# 🧩 Modular Design

The system is designed with a modular approach so that individual components can be upgraded or replaced without redesigning the entire system.

Possible future modules include:

* Improved pH sensing
* Additional water-quality sensors
* Higher-capacity dosing systems
* Improved mixing mechanisms
* Wireless monitoring
* Cloud data storage
* Advanced data analysis
* Additional chemical dosing channels
* Automated calibration

This modular structure allows the system to be improved according to future requirements.

---

# 🚀 Future Improvements

The following features can be added in future versions:

### 🤖 AI Voice Assistant

An AI-based voice assistant could be added to allow users to interact with the system using voice commands.

For example:

> "Start titration."

> "Show the current pH."

> "Stop the system."

### ☀️ Solar-Powered Operation

A solar power system could be integrated in the future to charge a rechargeable battery and provide an alternative power source.

This could improve the system's ability to operate in locations where a continuous electrical supply is not available.

### 📊 Advanced Data Analysis

Future versions could include advanced analysis of historical titration data, automatic comparison of experiments, and improved concentration calculations.

### 📱 Improved Mobile Application

A dedicated mobile application could provide:

* Real-time pH monitoring
* Temperature monitoring
* Manual dosing control
* Titration start/stop control
* Graph visualization
* Experimental history
* Notifications and error alerts

---

# 🛠️ Technologies & Components

### Hardware

* ESP32-based control system
* pH sensor
* Temperature sensor
* 2 × 50 mL syringes
* 2 × Stepper motors
* Stepper motor drivers
* Brushed DC motor
* Neodymium N48 magnets
* 25 × 8 mm magnetic stir bar
* 3D-printed mechanical components
* PWM motor control
* LED indicators
* Fuse protection
* Emergency power switch
* Protective enclosure

### Software

* Embedded firmware
* Mobile/web-based control interface
* Automated titration control
* pH and temperature data processing
* Data logging
* Graph generation
* Experimental report generation

---

# 🏗️ System Architecture

```text
                 ┌──────────────────────┐
                 │      User / Phone    │
                 └──────────┬───────────┘
                            │
                            ▼
                 ┌──────────────────────┐
                 │   Control System     │
                 │       ESP32          │
                 └───────┬───────┬──────┘
                         │       │
              ┌──────────┘       └──────────┐
              ▼                             ▼
       ┌─────────────┐               ┌─────────────┐
       │ pH Sensor   │               │ Temperature │
       │             │               │   Sensor    │
       └─────────────┘               └─────────────┘
                         │
                         ▼
                 ┌────────────────┐
                 │ Titration      │
                 │ Control        │
                 └───────┬────────┘
                         │
             ┌───────────┴───────────┐
             ▼                       ▼
      ┌──────────────┐       ┌──────────────┐
      │ Stepper Motor│       │ Stepper Motor│
      │ + Syringe 1  │       │ + Syringe 2  │
      └──────────────┘       └──────────────┘
                         │
                         ▼
                 ┌────────────────┐
                 │     Beaker     │
                 │                │
                 │  pH Sensor     │
                 │  Temp Sensor   │
                 │  Stir Bar      │
                 └───────┬────────┘
                         │
                         ▼
                  Titration Result
                         │
                         ▼
                Excel / Data Report
```

---

# 🎯 Expected Benefits

The Automated Titration System is designed to provide the following benefits:

* **Reduced human error**
* **Improved dosing accuracy**
* **Reduced chemical waste**
* **Reduced experiment time**
* **Lower operating cost**
* **Improved laboratory safety**
* **Repeatable experimental results**
* **Easy data recording**
* **Automatic graph generation**
* **Historical experiment analysis**
* **User-friendly operation**
* **Possibility of remote/manual control**

---

# 📈 Project Vision

The long-term vision of this project is to develop the Automated Titration System into a reliable and modular **automated water-quality and chemical analysis platform**.

The system can start as an automated titration device and gradually develop into a larger platform capable of supporting research laboratories, drinking-water treatment, industrial water treatment, aquaculture, and agricultural water-management applications.

---

# 👨‍💻 Project Status

**Current Status:** Prototype / Development

The current system includes automated titration, pH and temperature measurement, automatic chemical dosing, controlled mixing, safety features, status indicators, and experimental data recording.

Future development will focus on improving accuracy, automation, mobile control, data analysis, and integration with larger water-treatment systems.

---

## 📜 License

This project is developed for educational, research, and prototype development purposes.

---

## ⭐ Project

**Automated Titration System**

> Automating titration to make laboratory testing safer, easier, faster, and more reliable.
