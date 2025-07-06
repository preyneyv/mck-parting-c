# generate C lookup tables for the game

import math
import struct

def format_table(array, dtype='h'):
    return struct.pack(f'{len(array)}{dtype}', *array).hex().replace(' ', ', ')

    
def generate_synth_tables():
    # Generate sine wave table
    TABLE_SIZE = 256
    sine_wave = [math.sin(2 * math.pi * f / TABLE_SIZE) for f in range(TABLE_SIZE)]
    print('const float sine_wave[256] = {')
    print(format_table(sine_wave))
    print('};')


if __name__ == '__main__':
    generate_synth_tables()
