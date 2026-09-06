// 服务端 API 封装（JSON over HTTP）
const app = getApp()

function base() {
  return (app && app.globalData && app.globalData.apiBase) || 'http://127.0.0.1:8080'
}

/**
 * 统一请求
 * @param {string} path 接口路径
 * @param {string} method GET/POST
 * @param {object} data 请求体
 */
function request(path, method, data) {
  return new Promise((resolve, reject) => {
    wx.request({
      url: base() + path,
      method: method || 'GET',
      data: data || {},
      header: { 'Content-Type': 'application/json' },
      timeout: 10000,
      success(res) {
        if (res.statusCode === 200) {
          resolve(res.data)
        } else {
          reject({ errMsg: 'HTTP ' + res.statusCode, data: res.data })
        }
      },
      fail(err) {
        reject(err)
      }
    })
  })
}

module.exports = {
  base,
  request,

  // 定价查询
  getRooms() {
    return request('/api/rooms/pricing')
  },
  getVenues() {
    return request('/api/venues/pricing')
  },

  // 预订
  createBooking(data) {
    return request('/api/booking', 'POST', data)
  },
  getBookings() {
    return request('/api/bookings')
  },

  // 支付
  payCreate(data) {
    return request('/api/pay/create', 'POST', data)
  },

  // 语音/自然语言对话（多轮引导报价）
  conv(data) {
    return request('/api/conv', 'POST', data)
  },
  payMock(orderId) {
    return request('/api/pay/mock', 'POST', { order_id: orderId })
  },
  payNotify(data) {
    return request('/api/pay/notify', 'POST', data)
  },
  payStatus(orderId) {
    return request('/api/pay/status?order_id=' + encodeURIComponent(orderId))
  },
  payOrders() {
    return request('/api/pay/orders')
  }
}
