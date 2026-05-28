import json
import psycopg2
import os

DB_CONFIG = {
    "host": "localhost",
    "database": "network_monitor",
    "user": "mac",
    "password": "",
    "port": 5432
}

def get_signal_quality(rsrp):
    """Определение качества сигнала по RSRP"""
    if rsrp is None or rsrp == 0:
        return "Unknown"
    if rsrp > -80:
        return "Excellent"
    if rsrp > -90:
        return "Good"
    if rsrp > -100:
        return "Fair"
    if rsrp > -110:
        return "Poor"
    return "Very Poor"

def import_data(json_path):
    """Импорт данных из JSON в PostgreSQL"""
    
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()
        print("Connected to PostgreSQL")
    except Exception as e:
        print(f"Connection failed: {e}")
        return False
    
    cur.execute("SELECT COUNT(*) FROM signal_measurements")
    before_count = cur.fetchone()[0]
    print(f"Records before import: {before_count}")
    
    count = 0
    errors = 0
    
    with open(json_path, 'r') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
                
            try:
                data = json.loads(line)
                
                cur.execute("""
                    INSERT INTO signal_measurements (
                        device_timestamp,
                        time_str,
                        latitude,
                        longitude,
                        altitude,
                        rsrp,
                        rsrq,
                        rssi,
                        frequency,
                        network_type,
                        operator_name,
                        ip_address,
                        device_id,
                        signal_quality
                    ) VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                """, (
                    data.get('time', 0),
                    data.get('time_str', ''),
                    float(data.get('latitude', 0.0)),
                    float(data.get('longitude', 0.0)),
                    float(data.get('altitude', 0.0)),
                    int(data.get('rsrp', -140)),
                    int(data.get('rsrq', -20)),
                    int(data.get('rssi', -100)),
                    int(data.get('frequency', 0)),
                    str(data.get('network_type', 'Unknown')),
                    str(data.get('operator', 'Unknown')),
                    str(data.get('ip_address', '0.0.0.0')),
                    str(data.get('device_id', 'Unknown')),
                    get_signal_quality(data.get('rsrp'))
                ))
                count += 1
                
                if count % 1000 == 0:
                    conn.commit()
                    print(f"  Imported {count} records...")
                    
            except Exception as e:
                errors += 1
                if errors <= 5:
                    print(f"  Error at line {line_num}: {e}")
                continue
    
    conn.commit()
    
    cur.execute("SELECT COUNT(*) FROM signal_measurements")
    after_count = cur.fetchone()[0]
    
    print(f"\nImport summary:")
    print(f"  - Records read: {line_num}")
    print(f"  - Successfully inserted: {count}")
    print(f"  - Errors: {errors}")
    print(f"  - Records before: {before_count}")
    print(f"  - Records after: {after_count}")
    
    if after_count - before_count != count:
        print(f"Warning: Inserted {count} but difference is {after_count - before_count}")
    
    cur.execute("""
        SELECT id, latitude, longitude, rsrp, rsrq, network_type, signal_quality 
        FROM signal_measurements 
        LIMIT 5
    """)
    print("\nSample data:")
    for row in cur.fetchall():
        print(f"  ID:{row[0]}, Lat:{row[1]:.4f}, Lon:{row[2]:.4f}, RSRP:{row[3]}, Quality:{row[5]}")
    
    cur.close()
    conn.close()
    
    return count > 0

if __name__ == "__main__":
    possible_paths = [
        "location.json",
        "../location.json",
        "../../location.json",
        "/Users/mac/Documents/1/android_submodule/android2/examples/location.json",
    ]
    
    json_file = None
    for path in possible_paths:
        if os.path.exists(path):
            json_file = path
            print(f"Found JSON file: {json_file}")
            break
    
    if not json_file:
        print("location_log.json not found!")
        print(f"Current directory: {os.getcwd()}")
        print("Files in current directory:")
        for f in os.listdir('.'):
            print(f"  - {f}")
        exit(1)
    
    import_data(json_file)