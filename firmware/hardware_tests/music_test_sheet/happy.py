from reportlab.pdfgen import canvas
from reportlab.lib.units import mm

def generate_test_sheet(filename="ode_to_joy_A4_Pages.pdf"):
    # ==========================================
    # 🎵 音樂設定：歡樂頌 (Ode to Joy)
    # ==========================================
    test_sequence = [
        3, 3, 4, 5, 5, 4, 3, 2, 1, 1, 2, 3, 3, 2, 2,  # 前半段
        3, 3, 4, 5, 5, 4, 3, 2, 1, 1, 2, 3, 2, 1, 1   # 後半段
    ]

    # ==========================================
    # 📏 物理尺寸與時序設定
    # ==========================================
    black_length = 18 * mm        
    white_space = 15 * mm         
    beat_length = black_length + white_space 
    
    clock_offset = 4 * mm         
    clock_length = 10 * mm        

    start_x = 30 * mm          
    start_y = 60 * mm          
    track_pitch = 11.7 * mm      
    note_height = 5 * mm         
    margin = 10.05 * mm          
    
    # A4 橫向紙張設定 (297mm x 210mm)
    page_width = 297 * mm
    page_height = 210 * mm
    max_x = page_width - 15 * mm # 設定最右邊界，超過就換頁

    c = canvas.Canvas(filename, pagesize=(page_width, page_height))

    # --- 繪製背景網格與輔助線的函式 ---
    def draw_bg(page_num):
        # 畫 8 條淺灰色輔助線
        c.setStrokeColorRGB(0.8, 0.8, 0.8) 
        c.setLineWidth(0.1*mm)
        for i in range(8):
            y = start_y + (i * track_pitch)
            c.line(10*mm, y, max_x, y)

        # 畫紙帶邊緣裁切虛線 (總寬 102mm)
        c.setStrokeColorRGB(0, 0, 0)  
        c.setLineWidth(0.5 * mm)      
        c.setDash(4, 4)               
        cut_y_bottom = start_y - margin
        cut_y_top = start_y + (7 * track_pitch) + margin
        c.line(10*mm, cut_y_bottom, max_x, cut_y_bottom)
        c.line(10*mm, cut_y_top, max_x, cut_y_top)
        c.setDash(1, 0) # 恢復實線

        # 如果是第 2 頁以後，在左側畫上灰色黏接區 (GLUE TAB)
        if page_num > 1:
            c.setFillColorRGB(0.9, 0.9, 0.9)
            c.rect(10*mm, cut_y_bottom, 20*mm, cut_y_top - cut_y_bottom, fill=1, stroke=0)
            
            c.setFillColorRGB(0.4, 0.4, 0.4)
            c.setFont("Helvetica-Bold", 12)
            c.drawString(13*mm, start_y + (4 * track_pitch), "GLUE")
            c.drawString(13*mm, start_y + (3 * track_pitch), "TAB")

    # ==========================================
    # 📄 開始繪製紙帶內容
    # ==========================================
    current_x = start_x
    page_num = 1
    draw_bg(page_num)

    c.setFillColorRGB(0, 0, 0) 

    # 🌟 繪製第一頁開頭的全自動校正條
    for i in range(7): 
        y = start_y + (i * track_pitch) - (note_height / 2)
        c.rect(current_x, y, black_length, note_height, fill=1, stroke=0)
    
    current_x += (beat_length * 2.5) 

    # 🎵 繪製音樂資料陣列
    for state_id in test_sequence:
        # 如果畫完這個音符會超出 A4 紙邊界，就執行「裁切與換頁」
        if current_x + beat_length > max_x:
            # 在目前頁面畫上紅色的裁切提示線
            c.setStrokeColorRGB(1, 0, 0)
            c.setDash(2, 2)
            c.line(current_x, start_y - margin, current_x, start_y + (7 * track_pitch) + margin)
            c.setFillColorRGB(1, 0, 0)
            c.setFont("Helvetica-Bold", 10)
            c.drawString(current_x - 22*mm, start_y + (7.5 * track_pitch), "CUT HERE ->")
            c.setDash(1,0)
            
            # 建立新分頁
            c.showPage()
            page_num += 1
            draw_bg(page_num)
            
            # 歸零 X 座標，從新頁面的黏接區之後開始畫
            current_x = start_x
        
        # 將狀態碼轉成 6-bit 二進位字串，並反轉 LSB
        binary_str = format(state_id, '06b')[::-1]
        
        c.setFillColorRGB(0, 0, 0) # 恢復畫筆為黑色
        # 1. 繪製資料軌
        for track_index, bit_char in enumerate(binary_str):
            if bit_char == '1':
                y = start_y + (track_index * track_pitch) - (note_height / 2)
                c.rect(current_x, y, black_length, note_height, fill=1, stroke=0)
        
        # 2. 繪製時脈軌
        clock_y = start_y + (6 * track_pitch) - (note_height / 2)
        c.rect(current_x + clock_offset, clock_y, clock_length, note_height, fill=1, stroke=0)
        
        # 推進 1 拍
        current_x += beat_length

    c.save()
    print(f"✅ 歡樂頌 A4 分頁版已生成：{filename}")
    print(f"👉 總共分成 {page_num} 頁，請以「實際大小 (100%)」列印以確保 102mm 尺寸精確！")

if __name__ == "__main__":
    generate_test_sheet()