import os
import sys
from fontTools import subset

def get_char_list():
    chars = set()
    # 1. GB2312 complete character set (6763 Chinese characters + 682 symbols)
    for b1 in range(0xA1, 0xF8):
        for b2 in range(0xA1, 0xFF):
            try:
                c = bytes([b1, b2]).decode('gb2312')
                chars.add(c)
            except UnicodeDecodeError:
                pass

    # 2. Additional common punctuation and symbols
    extra = (
        "“”‘’—…·¥€©®™§¶†‡•✓✗★☆○●△▲※℃￥"
        "～！？；：（）【】《》〈〉「」『』〔〕［］｛｝"
        "0123456789"
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "`~!@#$%^&*()-_=+[{]}\\|;:'\",<.>/?"
    )
    chars.update(extra)
    return "".join(sorted(chars))

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    fonts_dir = os.path.join(root_dir, "page", "fonts")
    
    char_list = get_char_list()
    print(f"Subsetting with {len(char_list)} unique characters...")
    
    char_file = os.path.join(fonts_dir, "subset_chars.txt")
    with open(char_file, "w", encoding="utf-8") as f:
        f.write(char_list)
        
    targets = [
        ("SourceHanSerifSC-Regular.otf", "SourceHanSerifSC-Regular.woff2"),
        ("SourceHanSerifSC-SemiBold.otf", "SourceHanSerifSC-SemiBold.woff2")
    ]
    
    for input_name, output_name in targets:
        input_path = os.path.join(fonts_dir, input_name)
        output_path = os.path.join(fonts_dir, output_name)
        if not os.path.exists(input_path):
            print(f"Skipping {input_name}, file not found.")
            continue
            
        print(f"Processing {input_name} -> {output_name}...")
        options = subset.Options()
        options.flavor = 'woff2'
        options.desubroutinize = True
        options.layout_features = ["*"]
        
        font = subset.load_font(input_path, options)
        subsetter = subset.Subsetter(options=options)
        subsetter.populate(text=char_list)
        subsetter.subset(font)
        subset.save_font(font, output_path, options)
        
        orig_sz = os.path.getsize(input_path) / (1024 * 1024)
        new_sz = os.path.getsize(output_path) / (1024 * 1024)
        print(f"Done: {orig_sz:.2f} MB -> {new_sz:.2f} MB (Reduced by {(1 - new_sz/orig_sz)*100:.1f}%)")
        
    if os.path.exists(char_file):
        os.remove(char_file)

if __name__ == "__main__":
    main()
