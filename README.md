Markdown
# 🌿 SmartFarm: Integrated IoT Automation & Robotic Logistics

> **A Multi-tier System Integration Project for Autonomous Agriculture Management**

[![Language - Python](https://img.shields.io/badge/Python-3.9+-3776AB?style=flat-square&logo=python&logoColor=white)](https://www.python.org/)
[![Language - C++](https://img.shields.io/badge/C++-ESP32-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)
[![Platform - AWS](https://img.shields.io/badge/AWS-EC2_MySQL-232F3E?style=flat-square&logo=amazon-aws&logoColor=white)](https://aws.amazon.com/)
[![Framework - PyQt5](https://img.shields.io/badge/Framework-PyQt5-41CD52?style=flat-square&logo=qt&logoColor=white)](https://www.riverbankcomputing.com/software/pyqt/)

---

## 🏗️ System Architecture & Data Integration

This project demonstrates **Full-stack System Integration**, bridging the gap between embedded hardware, centralized cloud databases, and desktop monitoring interfaces.

```mermaid
graph TD
    subgraph Edge_Layer [Edge Layer: IoT & Robotics]
        FarmNode["Farm Firmware (ESP32)<br/>Environment Control"]
        RobotNode["Robot Firmware (ESP32-CAM)<br/>Autonomous Logistics"]
    end

    subgraph Server_Layer [Central Control Layer]
        MainServer["Python Control Server<br/>(Data Processing)"]
        AWS_DB[("AWS EC2<br/>MySQL Database")]
    end

    subgraph User_Interface [Operator Layer]
        AdminGUI["PyQt5 Dashboard<br/>(Real-time Monitoring)"]
    end

    %% Data Flow
    FarmNode <-->|"WiFi (TCP/IP)"| MainServer
    RobotNode <-->|"WiFi (TCP/IP)"| MainServer
    MainServer <-->|"PyMySQL"| AWS_DB
    MainServer <-->|"Socket/Signal"| AdminGUI
🚀 Key Engineering Roles
1. Central Control Server (Python)
Database Management: Engineered a robust DatabaseManager using PyMySQL to handle asynchronous data logging to AWS EC2.

Centralized Logic: Acts as the system brain, synchronizing states between distributed ESP32 nodes and the management GUI.

2. Autonomous Logistics Robot (C++ / ESP32-CAM)
Hardware Integration: Developed firmware for motor control and image streaming via ESP32-CAM.

Field Reliability: Implemented stable communication protocols to ensure seamless crop transportation within the farm.

3. Environment Automation (C++ / ESP32)
Sensor Fusion: Integrated multiple environmental sensors (Temp/Humi) for real-time climate monitoring.

Automated Actuation: Logic-driven control for irrigation and ventilation systems.

4. Management Dashboard (PyQt5)
Operator UX: Designed a comprehensive GUI for real-time data visualization and manual override capabilities.

🛠️ Tech Stack & Requirements
Languages: Python 3.x, C++ (Arduino/PlatformIO)

Infrastructure: AWS EC2 (MariaDB/MySQL)

Library/Framework: PyMySQL, PyQt5, ESP32 Board Manager

Protocols: TCP/IP, HTTP, Serial Communication

🤝 Contributing & Maintenance
To maintain professional code standards, this repository follows the Conventional Commits specification:

feat: New modules or hardware support.

fix: Bug fixes in firmware or server logic.

refactor: Optimization for field performance.

📩 Contact & Connect
Pinky Team · Robotics & System Integration

Designed for Industrial Automation | Powered by AWS & ESP32
