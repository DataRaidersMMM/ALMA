# 🤖 Proyecto ALMA  
### *Un robot con corazón para acompañar a las personas mayores*

**ALMA** es un robot de compañía diseñado para ofrecer apoyo, cariño y seguridad a las personas mayores. Nace del deseo de unir tecnología y empatía en un mismo proyecto, combinando sensores biomédicos, movimiento expresivo y comunicación visual para crear una experiencia cercana y humana.  

El proyecto fue desarrollado por un equipo participante en la **World Robot Olympiad (WRO) – categoría Future Engineers**, con el objetivo de explorar cómo la robótica puede mejorar la calidad de vida y el bienestar emocional de nuestros mayores.  

---

## 💡 ¿Qué hace ALMA?
ALMA no es solo un robot: es un compañero.  
Su funcionamiento está centrado en la interacción y el cuidado:  

- ❤️ **Mide el pulso y el oxígeno en sangre** con el sensor **MAX30102**.  
- 🌡️ **Toma la temperatura corporal** mediante el sensor **GY-906 (MLX90614)**.  
- 👀 **Muestra emociones y mensajes** en una pantalla **OLED** de 2.4 pulgadas.  
- ✋ **Levanta los brazos con servos** para invitar a la interacción durante las mediciones.  
- 🔊 **Habla con el usuario** mediante un módulo **MP3**, transmitiendo mensajes de voz amables.  
- 💬 **Reconoce el momento de la medición** al presionar un botón táctil con forma de corazón.  

---

## 🧠 Hardware y tecnologías
ALMA utiliza componentes accesibles pero potentes:  

- **ESP32-WROOM-32** como unidad principal  
- **Sensor MAX30102** (pulso y SpO₂)  
- **Sensor GY-906 / MLX90614** (temperatura corporal sin contacto)  
- **Pantalla OLED SPI 2.4" Waveshare**  
- **2 servos SG90** para los brazos  
- **Botón táctil capacitivo en forma de corazón**  
- **Módulo DFPlayer Mini (MP3)** para voz  
- **Fuente de alimentación LiPo 3.7V**

---

## 💬 Interacción paso a paso
1. El usuario pulsa el botón con forma de corazón 💖.  
2. ALMA levanta su brazo derecho y mide el pulso y la saturación de oxígeno.  
3. Luego, levanta el brazo izquierdo para tomar la temperatura.  
4. En pantalla aparecen los resultados junto a un mensaje de ánimo.  
5. Finalmente, ALMA agradece la interacción con una voz cálida y una expresión sonriente.  
