# MAC Consistency Test

## Vấn đề đã Fix:

### 🔴 Vấn đề cũ:
- Constructor `MeshSecurityConfig` tự động generate random keys
- Mỗi device có keys khác nhau
- Expected MAC ≠ Received MAC

### ✅ Giải pháp:
1. **Fixed constructor**: Không generate random keys nữa
2. **Centralized keys**: Tất cả devices sử dụng keys từ `mesh_security_keys.h`
3. **Enhanced logging**: Debug network keys và MAC calculation
4. **Explicit key setting**: Keys được set rõ ràng trong `initializeMeshSecurity()`

## Cách Test:

1. **Flash code mới lên cả 2 devices**
2. **Kiểm tra logs khi boot** - network keys phải giống nhau:
   ```
   [I][MeshSecurityInit] Setting Network Key: 2B7E15162BAE...
   [I][MeshSecurityInit] Setting Auth Token: 12345678...
   ```

3. **Khi gửi packet**, xem log:
   ```
   [D][SecurePacket] Generating MAC with Network Key: 2B7E15162BAE...
   [D][SecurePacket] Generated MAC: XXXXXXXX
   ```

4. **Khi nhận packet**, MAC sẽ match:
   ```
   [D][SecurePacket] Using Network Key: 2B7E15162BAE...
   [D][SecurePacket] Expected MAC: XXXXXXXX
   [D][SecurePacket] Received MAC: XXXXXXXX  ← Phải giống Expected MAC
   ```

## Kết quả mong đợi:
- ✅ Expected MAC = Received MAC  
- ✅ "MAC verification successful"
- ✅ "Packet decrypted successfully"
- ❌ Không còn "Packet authentication failed"