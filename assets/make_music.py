from reportlab.pdfgen import canvas
from reportlab.lib.units import mm

def generate_test_sheet(filename="sensor_test_sheet_102mm.pdf"):
    # 設定紙張為橫向 A4 (297mm x 210mm)
    c = canvas.Canvas(filename, pagesize=(297*mm, 210*mm))
    
    # --- 物理參數設定 ---
    start_x = 30 * mm          
    start_y = 60 * mm          
    track_pitch = 11.7 * mm      # 🌟 更改：中心點距 11.7mm
    beat_length = 30 * mm      
    note_height = 5 * mm       
    
    # --- 黑點長度與時序設定 (Setup & Hold Time) ---
    data_length = 18 * mm         # 資料軌黑點長度
    clock_offset = 4 * mm         # Clock 晚 4mm 才開始
    clock_length = 10 * mm        # Clock 提早結束，長度僅 10mm
    
    # --- 畫 8 條淺灰色輔助線 (0 到 7 軌) ---
    c.setStrokeColorRGB(0.8, 0.8, 0.8) 
    c.setLineWidth(0.1*mm)
    for i in range(8):
        y = start_y + (i * track_pitch)
        c.line(start_x, y, start_x + (8 * beat_length), y)

    # --- 畫紙帶邊緣裁切虛線 (總寬 102mm) ---
    c.setStrokeColorRGB(0, 0, 0)  
    c.setLineWidth(0.5 * mm)      
    c.setDash(4, 4)               

    # 🌟 依照精算：(102 - 81.9) / 2 = 10.05 mm (上下各留 10.05mm 餘裕)
    margin = 10.05 * mm
    cut_y_bottom = start_y - margin
    cut_y_top = start_y + (7 * track_pitch) + margin

    c.line(start_x - (5 * mm), cut_y_bottom, start_x + (8 * beat_length) + (5 * mm), cut_y_bottom)
    c.line(start_x - (5 * mm), cut_y_top, start_x + (8 * beat_length) + (5 * mm), cut_y_top)
    c.setDash(1, 0) # 恢復實線

    # --- 測試資料陣列 ---
    # 僅使用 6-bit 資料 (0~5軌)，最高可支援 63 種狀態
    test_sequence = [1, 2, 3, 4, 5, 6, 7]

    c.setFillColorRGB(0, 0, 0) 
    current_x = start_x   

    for state_id in test_sequence:
        # 將狀態碼轉成 6-bit 二進位字串，並反轉讓 LSB 到最下方 (第0軌)
        binary_str = format(state_id, '06b')[::-1]
        
        # 1. 繪製資料軌 (Tracks 0 ~ 5)
        for track_index, bit_char in enumerate(binary_str):
            if bit_char == '1':
                y = start_y + (track_index * track_pitch) - (note_height / 2)
                c.rect(current_x, y, data_length, note_height, fill=1, stroke=0)
        
        # 2. 繪製時脈軌 (Track 6，也就是第 7 軌)
        clock_y = start_y + (6 * track_pitch) - (note_height / 2)
        c.rect(current_x + clock_offset, clock_y, clock_length, note_height, fill=1, stroke=0)
        
        # X 軸往前推進 1 拍
        current_x += beat_length

    c.save()
    print(f"✅ 102mm 寬度 (軌距 11.7mm) 測試樂譜已生成：{filename}")
    print("👉 共 8 軌：第 8 軌裝飾，第 7 軌 Clock，0~5 軌為 6-bit 資料軌。")

if __name__ == "__main__":
    generate_test_sheet()